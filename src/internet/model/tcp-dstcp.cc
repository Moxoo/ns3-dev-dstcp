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

#include "tcp-dstcp.h"
#include "ns3/log.h"
#include "ns3/abort.h"
#include "ns3/tcp-socket-state.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TcpDstcp");

NS_OBJECT_ENSURE_REGISTERED (TcpDstcp);

TypeId TcpDstcp::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpDstcp")
    .SetParent<TcpLinuxReno> ()
    .AddConstructor<TcpDstcp> ()
    .SetGroupName ("Internet")
    .AddAttribute ("DstcpShiftG",
                   "Parameter G for updating dstcp_alpha",
                   DoubleValue (0.0625),
                   MakeDoubleAccessor (&TcpDstcp::m_g),
                   MakeDoubleChecker<double> (0, 1))
    .AddAttribute ("DstcpAlphaOnInit",
                   "Initial alpha value",
                   DoubleValue (1.0),
                   MakeDoubleAccessor (&TcpDstcp::InitializeDstcpAlpha),
                   MakeDoubleChecker<double> (0, 1))
    .AddAttribute ("DstcpBetaOnInit",
                   "Initial beta value",
                   DoubleValue (1.0),
                   MakeDoubleAccessor (&TcpDstcp::InitializeDstcpBeta),
                   MakeDoubleChecker<double> (0, 1))
    .AddAttribute ("DstcpTdcvOnInit",
                   "Initial Tdvc value",
                   UintegerValue (5),
                   MakeUintegerAccessor (&TcpDstcp::InitializeDstcpTdcv),
                   MakeUintegerChecker<uint32_t> ())
    .AddAttribute ("DstcpMinRtt",
                   "Initial min rtt",
                   TimeValue (MicroSeconds(100)),
                   MakeTimeAccessor (&TcpDstcp::m_minRtt),
                   MakeTimeChecker ())
    .AddAttribute ("UseEct0",
                   "Use ECT(0) for ECN codepoint, if false use ECT(1)",
                   BooleanValue (true),
                   MakeBooleanAccessor (&TcpDstcp::m_useEct0),
                   MakeBooleanChecker ())
    .AddTraceSource ("CongestionEstimate",
                     "Update sender-side congestion estimate state",
                     MakeTraceSourceAccessor (&TcpDstcp::m_traceCongestionEstimate),
                     "ns3::TcpDstcp::CongestionEstimateTracedCallback")
  ;
  return tid;
}

std::string TcpDstcp::GetName () const
{
  return "TcpDstcp";
}

TcpDstcp::TcpDstcp ()
  : TcpLinuxReno (),
    m_ackedBytesEcn (0),
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

TcpDstcp::TcpDstcp (const TcpDstcp& sock)
  : TcpLinuxReno (sock),
    m_ackedBytesEcn (sock.m_ackedBytesEcn),
    m_ackedBytesTotal (sock.m_ackedBytesTotal),
    m_priorRcvNxt (sock.m_priorRcvNxt),
    m_priorRcvNxtFlag (sock.m_priorRcvNxtFlag),
    m_alpha (sock.m_alpha),
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

TcpDstcp::~TcpDstcp (void)
{
  NS_LOG_FUNCTION (this);
}

Ptr<TcpCongestionOps> TcpDstcp::Fork (void)
{
  NS_LOG_FUNCTION (this);
  return CopyObject<TcpDstcp> (this);
}

void
TcpDstcp::Init (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  NS_LOG_INFO (this << "Enabling DctcpEcn for DSTCP");
  tcb->m_useEcn = TcpSocketState::On;
  tcb->m_ecnMode = TcpSocketState::DctcpEcn;
  tcb->m_ectCodePoint = m_useEct0 ? TcpSocketState::Ect0 : TcpSocketState::Ect1;
  m_initialized = true;
}

// Step 9, Section 3.3 of RFC 8257.  GetSsThresh() is called upon
// entering the CWR state, and then later, when CWR is exited,
// cwnd is set to ssthresh (this value).  bytesInFlight is ignored.
uint32_t
TcpDstcp::GetSsThresh (Ptr<const TcpSocketState> tcb, uint32_t bytesInFlight)
{
  NS_LOG_FUNCTION (this << tcb << bytesInFlight);
  NS_LOG_DEBUG ("bytesEcn != 0, reduce Cwnd by Ecn" << ", m_alpha = " << m_alpha);
  return static_cast<uint32_t> ( (1 - m_alpha / 2.0) * tcb->m_cWnd );
}

void
TcpDstcp::PktsAcked (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked, const Time &rtt)
{
  NS_LOG_FUNCTION (this << tcb << segmentsAcked << rtt);
  //std::cout << "dstcp rtt: " << rtt.GetMicroSeconds() << std::endl;

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
      NS_LOG_DEBUG ("Calculated tmp = " << tmp);

      targetCwnd = static_cast<uint32_t> (segCwnd * tmp);
      NS_LOG_DEBUG ("Calculated targetCwnd = " << targetCwnd);

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

  if (tcb->m_ecnState == TcpSocketState::ECN_ECE_RCVD)
    {
      m_ackedBytesEcn += segmentsAcked * tcb->m_segmentSize;
    }

  if (m_nextSeqFlag == false)
    {
      m_nextSeq = tcb->m_nextTxSequence;
      m_nextSeqFlag = true;
    }

  // rtt expire
  if (tcb->m_lastAckedSeq >= m_nextSeq)
    {
      double bytesEcn = 0.0; // Corresponds to variable M in RFC 8257
      double bytesRtt = 0.0;
      if (m_ackedBytesTotal >  0)
        {
          bytesEcn = static_cast<double> (m_ackedBytesEcn * 1.0 / m_ackedBytesTotal);
          bytesRtt = static_cast<double> (m_ackedBytesRtt * 1.0 / m_ackedBytesTotal);
          // NS_LOG_DEBUG ("bytesEcn " << bytesEcn <<", bytesRtt " << bytesRtt);
        }
      m_alpha = (1.0 - m_g) * m_alpha + m_g * bytesEcn;
      m_beta = (1.0 - m_g) * m_beta + m_g * bytesRtt;
      m_traceCongestionEstimate (m_ackedBytesEcn, m_ackedBytesTotal, m_alpha, m_beta);

      // n
      if(bytesEcn > 0)
        {
          // do nothing, but GetSsThresh() is called upon entering the CWR state
        }
      else
        {
          // no packet are marked by ECN, but some packets are marked by RTT reduce Cwnd right now
          if(bytesRtt > 0 && bytesEcn == 0)
            {
              NS_LOG_DEBUG ("bytesRtt > 0 and bytesEcn = 0, reduce Cwnd by Rtt"  << ", m_beta = " << m_beta);
              uint32_t val = static_cast<uint32_t> ( (1 - m_beta / 2.0) * tcb->m_cWnd );
              tcb->m_cWnd = std::max (val, 2 * tcb->m_segmentSize);
              tcb->m_ssThresh = tcb->m_cWnd;
              tcb->m_cWndInfl = tcb->m_cWnd;
            }
        }

      // if(bytesEcn > 0)
      //   {
      //     // NS_LOG_DEBUG ("bytesEcn != 0, " << "m_alpha " << m_alpha <<", m_beta " << m_beta);
      //   }
      // NS_LOG_DEBUG (this << "bytesEcn " << bytesEcn << ", m_alpha " << m_alpha << "m_beta " << m_beta);
      // NS_LOG_DEBUG ("m_alpha " << m_alpha <<", m_beta " << m_beta);

      Reset (tcb);
    }
}

void
TcpDstcp::InitializeDstcpAlpha (double alpha)
{
  NS_LOG_FUNCTION (this << alpha);
  NS_ABORT_MSG_IF (m_initialized, "DSTCP has already been initialized");
  m_alpha = alpha;
}

void
TcpDstcp::InitializeDstcpBeta (double beta)
{
  NS_LOG_FUNCTION (this << beta);
  NS_ABORT_MSG_IF (m_initialized, "DSTCP has already been initialized");
  m_beta = beta;
}

void
TcpDstcp::InitializeDstcpTdcv (uint32_t tdcv)
{
  NS_LOG_FUNCTION (this << tdcv);
  NS_ABORT_MSG_IF (m_initialized, "DSTCP has already been initialized");
  m_tdcv = tdcv;
}



void
TcpDstcp::Reset (Ptr<TcpSocketState> tcb)
{
  NS_LOG_FUNCTION (this << tcb);
  m_nextSeq = tcb->m_nextTxSequence;
  m_ackedBytesEcn = 0;
  m_ackedBytesRtt = 0;
  m_ackedBytesTotal = 0;
}

void
TcpDstcp::CeState0to1 (Ptr<TcpSocketState> tcb)
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
TcpDstcp::CeState1to0 (Ptr<TcpSocketState> tcb)
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
TcpDstcp::UpdateAckReserved (Ptr<TcpSocketState> tcb,
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
TcpDstcp::CwndEvent (Ptr<TcpSocketState> tcb,
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
