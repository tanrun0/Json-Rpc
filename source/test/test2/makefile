# 因为头文件是从 muduo开始包含的，所以只用找到 muduo
CFLAG= -std=c++11 -I ../../../build/release-install-cpp11/include
# -L : 找要依赖的库文件 ; -l 要链接的库   
LFLAG= -L../../../build/release-install-cpp11/lib  -lmuduo_net -lmuduo_base -pthread -ljsoncpp
all:server client reg_server
reg_server:regestry_server.cpp
	g++ -o $@ $^ $(CFLAG) $(LFLAG) -g
server:rpc_server.cpp
	g++ -o $@ $^ $(CFLAG) $(LFLAG) -g
client:rpc_client.cpp
	g++ -o $@ $^ $(CFLAG) $(LFLAG) -g
.PHONY:clean
clean:
	rm -rf client server reg_server