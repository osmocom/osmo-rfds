/*
 * sig_delay.c
 *
 * Signal delay line - runtime configurable length
 *
 * Copyright (C) 2018  sysmocom - systems for mobile communications GmbH
 *
 * vim: ts=4 sw=4
 */

`ifdef SIM
`default_nettype none
`endif

module sig_delay #(
	parameter integer WIDTH = 12
)(
	input  wire  data_valid,
	input  wire [WIDTH-1:0] data_in,
	output wire [WIDTH-1:0] data_out,
	input  wire [14:0] delay,
	input  wire clk,
	input  wire rst
);

	// Signals
	// -------

	reg [14:0] wr_addr;
	reg [14:0] rd_addr;
	wire ce;


	// Control
	// -------

	assign ce = data_valid;

	always @(posedge clk)
	begin
		if (rst) begin
			wr_addr <= 0;
			rd_addr <= 0;
		end else if (ce) begin
			wr_addr <= wr_addr + 1;
			rd_addr <= wr_addr - delay;
		end
	end


	// Storage
	// -------

	genvar i;
	
	generate
		for (i=0; i<WIDTH; i=i+1)
		begin
			// Signals
			wire [31:0] ram_do;
			wire [31:0] ram_di;

			// Connections
			assign data_out[i] = ram_do[0];
			assign ram_di = { 31'd0, data_in[i] };

			// Instantiate RAM Block
			RAMB36E1 #(
				.RDADDR_COLLISION_HWCONFIG("PERFORMANCE"),
				.SIM_COLLISION_CHECK("NONE"),
				.DOA_REG(1),
				.DOB_REG(1),
				.EN_ECC_READ("FALSE"),
				.EN_ECC_WRITE("FALSE"),
				.RAM_EXTENSION_A("NONE"),
				.RAM_EXTENSION_B("NONE"),
				.RAM_MODE("TDP"),
				.READ_WIDTH_A(1),
				.READ_WIDTH_B(1),
				.WRITE_WIDTH_A(1),
				.WRITE_WIDTH_B(1),
				.RSTREG_PRIORITY_A("RSTREG"),
				.RSTREG_PRIORITY_B("RSTREG"),
				.SIM_DEVICE("7SERIES"),
				.SRVAL_A(36'h000000000),
				.SRVAL_B(36'h000000000),
				.WRITE_MODE_A("READ_FIRST"),
				.WRITE_MODE_B("READ_FIRST")
			)
			mem_elem_I (
				.DOADO(ram_do),
				.DOPADOP(),
				.DOBDO(),
				.DOPBDOP(),
				.CASCADEINA(1'b0),
				.CASCADEINB(1'b0),
				.INJECTDBITERR(1'b0),
				.INJECTSBITERR(1'b0),
				.ADDRARDADDR({1'b1, rd_addr}),
				.CLKARDCLK(clk),
				.ENARDEN(ce),
				.REGCEAREGCE(ce),
				.RSTRAMARSTRAM(rst),
				.RSTREGARSTREG(rst),
				.WEA(4'd0),
				.DIADI(32'd0),
				.DIPADIP(4'd0),
				.ADDRBWRADDR({1'b1, wr_addr}),
				.CLKBWRCLK(clk),
				.ENBWREN(1'b1),
				.REGCEB(1'b0),
				.RSTRAMB(rst),
				.RSTREGB(rst),
				.WEBWE({7'd0, ce}),
				.DIBDI(ram_di),
				.DIPBDIP(4'd0)
			);
		end
	endgenerate

endmodule // sig_delay
