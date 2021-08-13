set -x

for seed in $(seq 9 10)
do
	./waf --run "examples/dstcp/leaf-spine --tcpTypeId=TcpDctcp --load=1 --randomSeed=${seed}"

	./waf --run "examples/dstcp/leaf-spine --tcpTypeId=TcpDcVegas --load=1 --randomSeed=${seed}"
	
	./waf --run "examples/dstcp/leaf-spine --tcpTypeId=TcpDstcp --load=1 --randomSeed=${seed}"
done
