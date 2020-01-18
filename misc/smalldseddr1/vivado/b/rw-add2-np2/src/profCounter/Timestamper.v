`timescale 1ns / 1ps

`include "commands.vh"

module Timestamper(
	/* Standard pins */
	clk,
	rst_n,

	/* Starts command unit */
	start,
	/* When a COMM_FINISH command is received, the module finishes execution and asserts done when finished */
	done,
	/* commandUnit command */
	command,

	/* Current timestamp */
	timestamp
);

	input clk;
	input rst_n;

	input start;
	output done;
	input [3:0] command;

	output [63:0] timestamp;

	reg [3:0] state;
	reg [63:0] counter;

	assign done = 'h0 == state;
	assign timestamp = counter;

	/* Counter logic */
	always @(posedge clk) begin
		if(!rst_n) begin
			counter <= 'h00;
			state <= 'h0;
		end
		else begin
			/* State 0x0: kernel is idle */
			if('h0 == state) begin
				if(start) begin
					counter <= 'h00;
					state <= 'h1;
				end
			end
			/* State 0x1: perform cycle count */
			else if('h1 == state) begin
				counter <= counter + 'h01;

				/* COMM_FINISH command: stop counting */
				if(`COMM_FINISH == command)
					state <= 'h0;
			end
			else begin
				state <= 'h0;
			end
		end
	end

endmodule
