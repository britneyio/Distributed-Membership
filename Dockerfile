FROM ubuntu:latest

RUN apt-get update && apt-get install -y gcc

ADD main.c /prj3/
ADD hostsfile.txt /prj3/
WORKDIR /prj3/ 
RUN gcc main.c -o main 

ENTRYPOINT ["/prj3/main"]