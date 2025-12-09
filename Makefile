CXXFLAGS := $(shell llvm-config --cxxflags)
LIBS     := $(shell llvm-config --libs)
LDFLAGS  := $(shell llvm-config --ldflags)

all: kak-lldb.so
	
kak-lldb.so: main.cc
	cc -fPIC -shared $< $(CXXFLAGS) $(LIBS) $(LDFLAGS) -o $@

install: kak-lldb.so
	cp $< ~/.config/kak

clean:
	rm kak-lldb
