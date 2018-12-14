/*
 * sig_chain_tb.v
 *
 * Copyright (C) 2018  sysmocom - systems for mobile communications GmbH
 *
 * vim: ts=4 sw=4
 */

`default_nettype none
`timescale 1ns/1ps

module sig_chain_tb;

	// Signals
	reg rst = 1;
	reg clk = 0;

	reg  data_valid;
	reg  [11:0] data_in;
	wire [11:0] data_out;
	wire [11:0] data_comb_3;
	wire [14:0] cfg_delay;

	// Setup recording
	initial begin
		$dumpfile("sig_chain_tb.vcd");
		$dumpvars(0,sig_chain_tb);
	end

	// Reset pulse
	initial begin
		# 21 rst = 0;
		# 50000 $finish;
	end

	// Clock
	always #5 clk = !clk;

	// DUT
	sig_delay #(
		.WIDTH(12)
	) dut_A_I (
		.data_valid(data_valid),
		.data_in(data_in),
		.data_out(data_out),
		.delay(cfg_delay),
		.clk(clk),
		.rst(rst)
	);

	sig_combine #(
		.D_WIDTH(12),
		.S_WIDTH(16),
		.S_FRAC(14)
	) dut_B_I (
		.in_data_0(data_out),
		.in_scale_0(16'h2000),	// 0.5
		.in_chain_0(data_in),
		.out_3(data_comb_3),
		.clk(clk),
		.rst(rst)
	);

	// Data gen
	always @(posedge clk)
		if (rst)
			data_in <= 0;
		else if (data_valid)
			data_in <= data_in + 1;

	always @(posedge clk)
		if (rst)
			data_valid <= 1'b0;
		else
			data_valid <= ($random & 3) == 0;

	// Config
	assign cfg_delay = 14'h00;

endmodule // sig_chain_tb
