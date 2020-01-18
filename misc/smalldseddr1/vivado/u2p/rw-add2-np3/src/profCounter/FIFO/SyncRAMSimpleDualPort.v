`timescale 1ns / 1ps

module SyncRAMSimpleDualPort#(
	parameter ADDR_WIDTH = 16,
	parameter DATA_WIDTH = 32
) (
	/* Standard pins */
	clk,

	/* Port A */
	enA,
	writeA,
	addressA,
	writeDataA,
	/* Port B */
	enB,
	addressB,
	readDataB
);

	input clk;

	input enA;
	input writeA;
	input [ADDR_WIDTH-1:0] addressA;
	input [DATA_WIDTH-1:0] writeDataA;
	input enB;
	input [ADDR_WIDTH-1:0] addressB;
	output [DATA_WIDTH-1:0] readDataB;

	reg [DATA_WIDTH-1:0] mem [0:(1 << ADDR_WIDTH)-1];
	reg [DATA_WIDTH-1:0] intReadDataB;

	assign readDataB = intReadDataB;

	always @(posedge clk) begin
		if(enA && writeA)
			mem[addressA] <= writeDataA;
	end

	always @(posedge clk) begin
		if(enB)
			intReadDataB <= mem[addressB];
	end

endmodule
