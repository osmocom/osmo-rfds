/*
 * sig_combine.v
 *
 * Simple signal combiner using DSP48E
 *
 * Copyright (C) 2018  sysmocom - systems for mobile communications GmbH
 *
 * vim: ts=4 sw=4
 */

`ifdef SIM
`default_nettype none
`endif

module sig_combine #(
	parameter integer D_WIDTH = 12,
	parameter integer S_WIDTH = 16,
	parameter integer S_FRAC  = 14
)(
	// Input
	input  wire [D_WIDTH-1:0] in_data_0,
	input  wire [S_WIDTH-1:0] in_scale_0,
	input  wire [D_WIDTH-1:0] in_chain_0,

	// Output
	output wire [D_WIDTH-1:0] out_3,

	// Control
	input  wire clk,
	input  wire rst
);
	localparam S = 25 - D_WIDTH + S_FRAC;

	// Signals
	reg  [17:0] in_chain_1;

	wire [47:0] pout_3;
	wire [29:0] a_0;
	wire [17:0] b_0;
	wire [47:0] c_1;

	// Align C
	always @(posedge clk)
		in_chain_1 <= in_chain_0;

	// Map in/out
	assign a_0 = { 5'd0, in_data_0,  { (25-D_WIDTH){1'b0} } };
	assign b_0 = { { (18-S_WIDTH){1'b0} }, in_scale_0 };
	assign c_1 = { { (48-S-D_WIDTH){in_chain_1[D_WIDTH-1]} }, in_chain_1, { (S){1'b0} } };

	assign out_3 = pout_3[S+:D_WIDTH];

	// DSP
	DSP48E1 #(
		.A_INPUT("DIRECT"),
		.B_INPUT("DIRECT"),
		.USE_DPORT("FALSE"),
		.USE_MULT("MULTIPLY"),
		.AUTORESET_PATDET("NO_RESET"),
		.MASK(48'h3fffffffffff),
		.PATTERN(48'h000000000000),
		.SEL_MASK("MASK"),
		.SEL_PATTERN("PATTERN"),
		.USE_PATTERN_DETECT("PATDET"),
		.ACASCREG(1),
		.ADREG(1),
		.ALUMODEREG(1),
		.AREG(1),
		.BCASCREG(1),
		.BREG(1),
		.CARRYINREG(1),
		.CARRYINSELREG(1),
		.CREG(1),
		.DREG(1),
		.INMODEREG(1),
		.MREG(1),
		.OPMODEREG(1),
		.PREG(1),
		.USE_SIMD("ONE48")
	) dsp_I (
		.P(pout_3),
		.ACIN(30'h0),
		.BCIN(18'h0),
		.CARRYCASCIN(1'h0),
		.MULTSIGNIN(1'h0),
		.PCIN(48'h000000000000),
		.ALUMODE(4'b0000),      // Z + X + Y + CIN
		.CARRYINSEL(3'h0),
		.CEINMODE(1'b1),
		.CLK(clk),
		.INMODE(5'b00000),      // B=B2, A=A2
		.OPMODE(7'b0110101),    // X=M1, Y=M2, Z=C
		.RSTINMODE(rst),
		.A(a_0),
		.B(b_0),
		.C(c_1),
		.CARRYIN(1'b0),
		.D(25'd0),
		.CEA1(1'b0),
		.CEA2(1'b1),
		.CEAD(1'b1),
		.CEALUMODE(1'b1),
		.CEB1(1'b0),
		.CEB2(1'b1),
		.CEC(1'b1),
		.CECARRYIN(1'b1),
		.CECTRL(1'b1),
		.CED(1'b1),
		.CEM(1'b1),
		.CEP(1'b1),
		.RSTA(rst),
		.RSTALLCARRYIN(rst),
		.RSTALUMODE(rst),
		.RSTB(rst),
		.RSTC(rst),
		.RSTCTRL(rst),
		.RSTD(rst),
		.RSTM(rst),
		.RSTP(rst)
	);

endmodule // sig_combine
