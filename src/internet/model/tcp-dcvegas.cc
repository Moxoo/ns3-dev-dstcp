/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2017 NITK Surathkal
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Shravya K.S. <shravya.ks0@gmail.com>
 *
 */

#include "tcp-dcvegas.h"
#include "ns3/log.h"
#include "ns3/abort.h"
#include "ns3/tcp-socket-state.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TcpDcVegas");

NS_OBJECT_ENSURE_REGISTERED (TcpDcVegas);

TypeId TcpDcVegas::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpDcVegas")
    .SetParent<TcpLinuxReno> ()
    .AddConstructor<TcpDcVegas> ()
    .SetGroupName ("Internet")
    .AddAttribute ("DcVegasShiftG",
                   "Parameter G for updating DcVegas_beta",
                   DoubleValue (0.0625),
                   MakeDoubleAccessor (&TcpDcVegas::m_g),
                   MakeDoubleChecker<double> (0, 1))
    .AddAttribute ("DcVegasBetaOnInit",
                   "Initial beta value",
                   DoubleValue (1.0),
                   MakeDoubleAccessor (&TcpDcVegas::InitializeDcVegasBeta),
                   MakeDoubleChecker<double> (0, 1))
    .AddAttribute ("DcVegasTdcvOnInit",
                   "Initial Tdvc value",
                   UintegerValue (5),
                   MakeUintegerAccessor (&TcpDcVegas::InitializeDcVegasTdcv),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("DcVegasMinRtt",
                   "Initial min rtt",
                   TimeValue (MicroSeconds(100)),
                   MakeTimeAccessor (&TcpDcVegas::m_minRtt),
                   MakeTimeChecker ())
    .AddAttribute ("UseEct0",
                   "Use ECT(0) for ECN codepoint, if false use ECT(1)",
                   BooleanValue (false),
                   MakeBooleanAccessor (&TcpDcVegas::m_useEct0),
                   MakeBooleanChecker ())
    .AddTraceSource ("CongestionEstimate",
                     "Update sender-side congestion estimate state",
                     MakeTraceSourceAccessor (&TcpDcVegas::m_traceCongestionEstimate),
                     "ns3::TcpDcVegas::CongestionEstimateTracedCallback")
  ;
  return tid;
}

std::string TcpDcVegas::GetName () const
{
  return "TcpDcVegas";
}

TcpDcVegas::TcpDcVegas ()
  : TcpLinuxReno (),
    m_ackedBytesRtt (0),
    m_ackedBytesTotal (0),
    m_priorRcvNxt (SequenceNumber32 (0)),
    m_priorRcvNxtFlag (false),
    m_nextSeq (SequenceNumber32 (0)),
    m_nextSeqFlag (false),
    m_ceState (false),
    m_delayedAckReserved (false),
    m_initialized (false)
    // m_minRtt (Time::Max ())
{
  NS_LOG_FUNCTION (this);
}

TcpDcVegas::TcpDcVegas (const TcpDcVegas& sock)
  : TcpLinuxReno (sock),
    m_ackedBytesRtt (sock.m_ackedBytesRtt),
    m_ackedBytesTotal (sock.m_ackedBytesTotal),
    m_priorRcvNxt (sock.m_priorRcvNxt),
    m_priorRcvNxtFlag (sock.m_priorRcvNxtFlag),
    m_beta (sock.m_beta),
    m_nextSeq (sock.m_nextSeq),
    m_nextSeqFlag (sock.m_nextSeqFlag),
    m_ceState (sock.m_ceState),
    m_delayedAckReserved (sock.m_delayedAckReserved),
    m_g (sock.m_g),
    m_useEct0 (sock.m_useEct0),
    m_initialized (sock.m_initialized),
    m_minRtt (sock.m_minRtt)
{
  NS_LOG_FUNCTION (this);
}

TcpDcVegas::~TcpDcVegas (void)
{
  NS_LOG_FUNCTION (this);
}

Ptr<TcpCongestionOps> TcpDcVegas::Fork (void)
{
  NS_LOG_FUNCTION (this);
  return CopyObject<TcpDcVegas> (this);
}

void
TcpDcVegas::Init (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  NS_LOG_INFO (this << "Disabling DctcpEcn for DcVegas");
  tcb->m_useEcn = TcpSocketState::Off;
//   tcb->m_ecnMode = TcpSocketState::DctcpEcn;
  tcb->m_ectCodePoint = m_useEct0 ? TcpSocketState::Ect0 : TcpSocketState::Ect1;
  m_initialized = true;
}

// Step 9, Section 3.3 of RFC 8257.  GetSsThresh() is called upon
// entering the CWR state, and then later, when CWR is exited,
// cwnd is set to ssthresh (this value).  bytesInFlight is ignored.
uint32_t
TcpDcVegas::GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight)
{
  NS_LOG_FUNCTION (this << tcb << bytesInFlight);
  return static_cast<uint32_t> ((1 - m_beta / 2.0) * tcb->m_cWnd);
}

void
TcpDcVegas::PktsAcked (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time &rtt)
{
  NS_LOG_FUNCTION (this << tcb << segmentsAcked << rtt);
  m_ackedBytesTotal += segmentsAcked * tcb->m_segmentSize;

  if (!rtt.IsZero ())
    {
      uint32_t nql;  //network queue length
      uint32_t targetCwnd;

      uint32_t segCwnd = tcb->GetCwndInSegments ();
      NS_LOG_DEBUG ("Calculated current Cwnd = " << segCwnd);

      m_minRtt = std::min (m_minRtt, rtt);
      NS_LOG_DEBUG ("current rtt (us): " << rtt.GetMicroSeconds() << " Updated minRtt (us): " << m_minRtt.GetMicroSeconds());
      std::cout << rtt.GetMicroSeconds() << " " << m_minRtt.GetMicroSeconds() << std::endl;

      double tmp = m_minRtt.GetMicroSeconds() * 1.0 / rtt.GetMicroSeconds();
      NS_LOG_INFO ("Calculated tmp = " << tmp);

      targetCwnd = static_cast<uint32_t> (segCwnd * tmp);
      NS_LOG_INFO ("Calculated targetCwnd = " << targetCwnd);

      NS_ASSERT (segCwnd >= targetCwnd); // implies minRtt <= Rtt

      /*
       * Calculate the network queue length
       */
      nql = segCwnd - targetCwnd;
      NS_LOG_DEBUG ("Calculated nql: " << nql);

      if (nql >= m_tdcv)
        {
          NS_LOG_DEBUG ("nql: " << nql << " >= tdcv: " << m_tdcv);
          m_ackedBytesRtt += segmentsAcked * tcb->m_segmentSize;
        }
    }

  if (m_nextSeqFlag == false)
    {
      m_nextSeq = tcb->m_nextTxSequence;
      m_nextSeqFlag = true;
    }

  // rtt expire
  if (tcb->m_lastAckedSeq >= m_nextSeq)
    {
      double bytesRtt = 0.0; // Corresponds to variable M in RFC 8257
      if (m_ackedBytesTotal >  0)
        {
          bytesRtt = static_cast<double> (m_ackedBytesRtt * 1.0 / m_ackedBytesTotal);
        }
      m_beta = (1.0 - m_g) * m_beta + m_g * bytesRtt;
      m_traceCongestionEstimate (m_ackedBytesRtt, m_ackedBytesTotal, m_beta);
      NS_LOG_DEBUG ("bytesRtt: " << bytesRtt << ", m_beta: " << m_beta);

      // decrease cwnd once in each rtt
      if(bytesRtt > 0)
        {
          uint32_t val = static_cast<uint32_t> ( (1 - m_beta / 2.0) * tcb->m_cWnd );
          tcb->m_cWnd = std::max (val, 2 * tcb->m_segmentSize);
          tcb->m_ssThresh = tcb->m_cWnd;
          tcb->m_cWndInfl = tcb->m_cWnd;
        }
      Reset (tcb);
    }
}

void
TcpDcVegas::InitializeDcVegasBeta (double beta)
{
  NS_LOG_FUNCTION (this << beta);
  NS_ABORT_MSG_IF (m_initialized, "DcVegas has already been initialized");
  m_beta = beta;
}

void
TcpDcVegas::InitializeDcVegasTdcv (uint32_t tdcv)
{
  NS_LOG_FUNCTION (this << tdcv);
  NS_ABORT_MSG_IF (m_initialized, "DcVegas has already been initialized");
  m_tdcv = tdcv;
}

void
TcpDcVegas::Reset (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  m_nextSeq = tcb->m_nextTxSequence;
  m_ackedBytesRtt = 0;
  m_ackedBytesTotal = 0;
}

void
TcpDcVegas::CeState0to1 (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  if (!m_ceState && m_delayedAckReserved && m_priorRcvNxtFlag)
    {
      SequenceNumber32 tmpRcvNxt;
      /* Save current NextRxSequence. */
      tmpRcvNxt = tcb->m_rxBuffer->NextRxSequence ();

      /* Generate previous ACK without ECE */
      tcb->m_rxBuffer->SetNextRxSequence (m_priorRcvNxt);
      tcb->m_sendEmptyPacketCallback (TcpHeader::ACK);

      /* Recover current RcvNxt. */
      tcb->m_rxBuffer->SetNextRxSequence (tmpRcvNxt);
    }

  if (m_priorRcvNxtFlag == false)
    {
      m_priorRcvNxtFlag = true;
    }
  m_priorRcvNxt = tcb->m_rxBuffer->NextRxSequence ();
  m_ceState = true;
  tcb->m_ecnState = TcpSocketState::ECN_CE_RCVD;
}

void
TcpDcVegas::CeState1to0 (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  if (m_ceState && m_delayedAckReserved && m_priorRcvNxtFlag)
    {
      SequenceNumber32 tmpRcvNxt;
      /* Save current NextRxSequence. */
      tmpRcvNxt = tcb->m_rxBuffer->NextRxSequence ();

      /* Generate previous ACK with ECE */
      tcb->m_rxBuffer->SetNextRxSequence (m_priorRcvNxt);
      tcb->m_sendEmptyPacketCallback (TcpHeader::ACK | TcpHeader::ECE);

      /* Recover current RcvNxt. */
      tcb->m_rxBuffer->SetNextRxSequence (tmpRcvNxt);
    }

  if (m_priorRcvNxtFlag == false)
    {
      m_priorRcvNxtFlag = true;
    }
  m_priorRcvNxt = tcb->m_rxBuffer->NextRxSequence ();
  m_ceState = false;

  if (tcb->m_ecnState == TcpSocketState::ECN_CE_RCVD || tcb->m_ecnState == TcpSocketState::ECN_SENDING_ECE)
    {
      tcb->m_ecnState = TcpSocketState::ECN_IDLE;
    }
}

void
TcpDcVegas::UpdateAckReserved (Ptr<TcpSocketState> tcb,
                             const TcpSocketState::TcpCAEvent_t event)
{
  NS_LOG_FUNCTION (this << tcb << event);
  switch (event)
    {
    case TcpSocketState::CA_EVENT_DELAYED_ACK:
      if (!m_delayedAckReserved)
        {
          m_delayedAckReserved = true;
        }
      break;
    case TcpSocketState::CA_EVENT_NON_DELAYED_ACK:
      if (m_delayedAckReserved)
        {
          m_delayedAckReserved = false;
        }
      break;
    default:
      /* Don't care for the rest. */
      break;
    }
}

void
TcpDcVegas::CwndEvent (Ptr<TcpSocketState> tcb,
                     const TcpSocketState::TcpCAEvent_t event)
{
  NS_LOG_FUNCTION (this << tcb << event);
  switch (event)
    {
    case TcpSocketState::CA_EVENT_ECN_IS_CE:
      CeState0to1 (tcb);
      break;
    case TcpSocketState::CA_EVENT_ECN_NO_CE:
      CeState1to0 (tcb);
      break;
    case TcpSocketState::CA_EVENT_DELAYED_ACK:
    case TcpSocketState::CA_EVENT_NON_DELAYED_ACK:
      UpdateAckReserved (tcb, event);
      break;
    default:
      /* Don't care for the rest. */
      break;
    }
}

} // namespace ns3
