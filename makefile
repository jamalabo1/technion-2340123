TESTS := $(sort $(wildcard test*.cxx))
SOURCES := $(filter-out $(TESTS),$(wildcard *.cxx))
HEADERS := $(wildcard *.h)
TARGETS := $(TESTS:.cxx=.exe)
RESULTS := $(TESTS:.cxx=.out)
GRADE := grade.txt
CXXFLAGS := -std=c++11 -O3

.PHONY: all test clean
all: $(TARGETS)

$(TARGETS): %.exe: %.cxx $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $< $(SOURCES) -o $@

test: $(GRADE)

$(GRADE): $(RESULTS)
	grep SUCCESS $^ | wc -l > $@

$(RESULTS): %.out: %.exe
	timeout --foreground 60 taskset -c 0 ./$< > $@ 2>&1

handin:
	echo "HANDIN";
	
	#cat /etc/os-release | grep "VERSION_ID" | grep -q "18.04"  # You should zip in the course VM

	if [ -d "./certs" ]; then \
    	echo "You are trying to pack the entire kernel source code - copy only the files you modified into a folder with this makefile and try again."; exit 1; \
	fi
	
	if [ -d "./crypto" ]; then \
    	echo "You are trying to pack the entire kernel source code - copy only the files you modified into a folder with this makefile and try again."; exit 1; \
	fi
	
	if [ -d "./debian" ]; then \
    	echo "You are trying to pack the entire kernel source code - copy only the files you modified into a folder with this makefile and try again."; exit 1; \
	fi
	
	if [ ! -f "./kernel/hw2.c" ]; then \
    	echo "kernel/hw2.c not found - are you in the right directory?"; exit 1; \
	fi
	if [ ! -f "../submitters.txt" ]; then \
    	echo "submitters.txt not found"; exit 1; \
	fi
	if [ ! -f "./kernel/Makefile" ]; then \
    	echo "/kernel/Makefile not found"; exit 1; \
	fi
	if [ ! -f "./arch/x86/entry/syscalls/syscall_64.tbl" ]; then \
    	echo "syscall_64.tbl not found"; exit 1; \
	fi
	if [ ! -f "./include/linux/syscalls.h" ]; then \
    	echo "syscalls.h not found"; exit 1; \
	fi

	if [ ! -d "./fs" ]; then \
		mkdir fs; \
	fi
	
	zip -r final.zip ../submitters.txt kernel/ fs/ include/ arch/
	
	echo "OK"

clean:
	rm -f $(TARGETS) $(RESULTS) $(GRADE)

