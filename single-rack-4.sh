set -x

for seed in $(seq 9 10)
do
	./waf --run "examples/dstcp/single-rack --tcpTypeId=TcpDctcp --load=1 --randomSeed=${seed}"

	./waf --run "examples/dstcp/single-rack --tcpTypeId=TcpDcVegas --load=1 --randomSeed=${seed}"
	
	./waf --run "examples/dstcp/single-rack --tcpTypeId=TcpDstcp --load=1 --randomSeed=${seed}"
done
