#
# Simulation Makefile
#

XILINX_LIBS=\
	xilinx/glbl.v		\
	xilinx/DSP48E1.v	\
	xilinx/RAMB36E1.v

OBJS=\
	sig_combine.v \
	sig_delay.v

TESTBENCHES=\
	sig_chain_tb

all: $(TESTBENCHES)

%_tb: %_tb.v $(XILINX_LIBS) $(OBJS)
	iverilog -Wall -DSIM=1 -o $@ $(XILINX_LIBS) $(OBJS) $<

clean:
	rm -f $(TESTBENCHES) *.vcd
