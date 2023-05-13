FROM ubuntu:latest
RUN apt-get update
RUN apt-get install make -y
RUN apt-get install autoconf -y
RUN apt-get install autoconf -y
RUN apt-get install g++ -y
ADD . /bfdd
WORKDIR /bfdd
RUN ./autogen.sh
RUN ./configure
RUN make
RUN make dist
ENTRYPOINT ["bfdd-beacon", "--listen=0.0.0.0"]