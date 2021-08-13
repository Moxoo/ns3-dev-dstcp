set -x

./waf --run "examples/tcp/dctcp-example --tcpTypeId=TcpDctcp"
./waf --run "examples/tcp/dctcp-example --tcpTypeId=TcpDcVegas"
./waf --run "examples/tcp/dctcp-example --tcpTypeId=TcpDstcp"

