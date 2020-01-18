`timescale 1ns / 1ps

module SequentialWriterTb;

	reg clk;
	reg rst_n;

	reg [63:0] offset;
	reg [3:0] command;
	reg [63:0] value;
	wire idle;

	wire axiAWVALID;
	reg axiAWREADY;
	wire [63:0] axiAWADDR;
	wire [7:0] axiAWLEN;
	wire [2:0] axiAWSIZE;
	wire axiWVALID;
	reg axiWREADY;
	wire [63:0] axiWDATA;
	wire [7:0] axiWSTRB;
	wire axiWLAST;
	reg [1:0] axiBRESP;
	reg axiBVALID;
	wire axiBREADY;

	/* DUT */
	SequentialWriter inst(
		.clk(clk),
		.rst_n(rst_n),

		.offset(offset),
		.command(command),
		.value(value),
		.idle(idle),

		.axiAWVALID(axiAWVALID),
		.axiAWREADY(axiAWREADY),
		.axiAWADDR(axiAWADDR),
		.axiAWLEN(axiAWLEN),
		.axiAWSIZE(axiAWSIZE),
		.axiWVALID(axiWVALID),
		.axiWREADY(axiWREADY),
		.axiWDATA(axiWDATA),
		.axiWSTRB(axiWSTRB),
		.axiWLAST(axiWLAST),
		.axiBRESP(axiBRESP),
		.axiBVALID(axiBVALID),
		.axiBREADY(axiBREADY)
	);

	initial begin
		$dumpfile("tb.vcd");
		$dumpvars;

		/* Reset */
		clk <= 'b1;
		rst_n <= 'b0;
		offset <= 'hDEADCAFE00;
		command <= 'h0;
		value <= 'hDEADBEEF00;
		axiAWREADY <= 'b1;
		axiWREADY <= 'b1;
		axiBRESP <= 'b00;
		axiBVALID <= 'b1;
		#200 @(posedge clk);

		/* Release reset */
		rst_n <= 'b1;
		#200 @(posedge clk);

		command <= 'h1;
		value <= 'hDEADBEEF00;
		#50 @(posedge clk);

		command <= 'h1;
		value <= 'hDEADBEEF10;
		#50 @(posedge clk);

		command <= 'h1;
		value <= 'hDEADBEEF20;
		#50 @(posedge clk);

		command <= 'h1;
		value <= 'hDEADCAFE00;
		#50 @(posedge clk);

		command <= 'h1;
		value <= 'hDEADCAFE10;
		#50 @(posedge clk);

		command <= 'h1;
		value <= 'hDEADCAFE20;
		#50 @(posedge clk);

		command <= 'h2;
		#50 @(posedge clk);

		command <= 'h0;
		#2000 @(posedge clk);

		$finish;
	end

	always begin
		#50 clk <= ~clk;
	end

endmodule
