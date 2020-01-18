`timescale 1ns / 1ps

/**
 * AXI4-Lite slave control interface with only basic features implemented
 *
 * Address Map | Name                    | Description
 *        0x00 | Control                 | Control Register
 *        0x04 | Global Interrupt Enable | Not supported
 *        0x08 | IP Interrupt Enable     | Not supported
 *        0x0C | IP Interrupt Status     | Not supported
 *   0x10-0x14 | Kernel argument "log"   | Pointer to global memory where the buffer for "log" variable is allocated
 *        0x18 | Reserved                | Reserved
 *        0x1C | Kernel pipe "p0"        | Not used, here for compatibility purposes (if applicable)
 *        0x20 | Reserved                | Reserved
 *
 * Control Register description
 * Bit(s) | Description                                     | Behaviour
 * [31:8] | Reserved                                        | ---
 *    [7] | Auto-restart                                    | Read/write
 *  [6:4] | Reserved                                        | ---
 *    [3] | Ready, asserted when module is ready to execute | Read-only
 *    [2] | Idle, asserted when module is idle              | Read-only
 *    [1] | Done, asserted when module finished execution   | Read-only, reset on read
 *    [0] | Start, asserted by master to start execution    | Read/write, reset when handshake is performed
 */
module BasicController#(
	parameter ADDR_WIDTH = 6
) (
	/* Standard pins */
	clk,
	rst_n,

	/* AXI4 Slave to OpenCL kernel controller */
	axiAWADDR,
	axiAWVALID,
	axiAWREADY,
	axiWDATA,
	axiWSTRB,
	axiWVALID,
	axiWREADY,
	axiBRESP,
	axiBVALID,
	axiBREADY,
	axiARADDR,
	axiARVALID,
	axiARREADY,
	axiRDATA,
	axiRRESP,
	axiRVALID,
	axiRREADY,

	/* Asserted by host when this kernel is enqueued for execution  */
	start,
	/* Asserted by this kernel when execution is done */
	done,
	/* Asserted by this kernel when kernel is ready to execute */
	ready,
	/* Asserted by this kernel when kernel is idling */
	idle,
	/* Base address for "log" global memory array */
	offset
);

	input clk;
	input rst_n;

	input [ADDR_WIDTH-1:0] axiAWADDR;
	input axiAWVALID;
	output axiAWREADY;
	input [31:0] axiWDATA;
	input [3:0] axiWSTRB;
	input axiWVALID;
	output axiWREADY;
	output [1:0] axiBRESP;
	output axiBVALID;
	input axiBREADY;
	input [ADDR_WIDTH-1:0] axiARADDR;
	input axiARVALID;
	output axiARREADY;
	output [31:0] axiRDATA;
	output [1:0] axiRRESP;
	output axiRVALID;
	input axiRREADY;

	output start;
	input done;
	input ready;
	input idle;
	output [63:0] offset;

	/* AXI4 write FSM registers */
	reg [1:0] wState;
	reg [1:0] wNextState;
	reg [ADDR_WIDTH-1:0] wAddr;
	wire [31:0] wMask;

	/* AXI4 read FSM registers */
	reg [1:0] rState;
	reg [1:0] rNextState;
	reg [31:0] rData;

	/* Registers accessible through AXI4 transactions */
	reg intStart;
	reg intDone;
	reg intRestart;
	reg [63:0] intOffset;
	reg [31:0] intPipe;

	/* Assign AXI4 write signals */
	assign axiAWREADY = rst_n && 'h0 == wState;
	assign axiWREADY = 'h1 == wState;
	assign axiBRESP = 'h0;
	assign axiBVALID = 'h2 == wState;
	assign wMask = {{8{axiWSTRB[3]}}, {8{axiWSTRB[2]}}, {8{axiWSTRB[1]}}, {8{axiWSTRB[0]}}};

	/* AXI4 write FSMs */
	always @(posedge clk) begin
		if(!rst_n)
			wState <= 'h0;
		else
			wState <= wNextState;
	end
	always @(*) begin
		case(wState)
			'h0:
				wNextState = axiAWVALID? 'h1 : 'h0;
			'h1:
				wNextState = axiWVALID? 'h2 : 'h1;
			'h2:
				wNextState = axiBREADY? 'h0 : 'h2;
			default:
				wNextState = 'h0;
		endcase
	end

	/* AXI4 write address FSM */
	always @(posedge clk) begin
		if(axiAWVALID && axiAWREADY)
			wAddr <= axiAWADDR;
	end

	/* Assign AXI4 read signals */
	assign axiARREADY = rst_n && 'h0 == rState;
	assign axiRDATA = rData;
	assign axiRRESP = 2'b00;
	assign axiRVALID = 'h1 == rState;

	/* AXI4 read FSMs */
	always @(posedge clk) begin
		if(!rst_n)
			rState <= 'h0;
		else
			rState <= rNextState;
	end
	always @(*) begin
		case(rState)
			'h0:
				rNextState <= (axiARVALID)? 'h1 : 'h0;
			'h1:
				rNextState <= (axiRREADY && axiRVALID)? 'h0 : 'h1;
			default:
				rNextState <= 'h0;
		endcase
	end

	/* Register read logic */
	always @(posedge clk) begin
		if(axiARVALID && axiARREADY) begin
			case(axiARADDR)
				/* 0x00: status register */
				'h00:
					begin
						rData[0] <= intStart;
						rData[1] <= intDone;
						rData[2] <= idle;
						rData[3] <= ready;
						rData[7] <= intRestart;
					end
				/* 0x10: LSB of "log" base address */
				'h10:
					begin
						rData <= intOffset[31:0];
					end
				/* 0x14: MSB of "log" base address */
				'h14:
					begin
						rData <= intOffset[63:32];
					end
				/* 0x1C: pipe "p0" content */
				'h1C:
					begin
						rData <= intPipe;
					end
				default:
					begin
						rData <= 'h0;
					end
			endcase
		end
	end

	assign start = intStart;
	assign offset = intOffset;

	/* Register write logic */
	always @(posedge clk) begin
		if(!rst_n) begin
			intStart <= 'b0;
			intDone <= 'b0;
			intRestart <= 'b0;
			intOffset <= 'h0;
			intPipe <= 'h0;
		end
		else begin
			/* If "start" bit is set in status register, generate a start signal. It clears after handshake */
			if(axiWVALID && axiWREADY && 'h00 == wAddr && axiWSTRB[0] && axiWDATA[0])
				intStart <= 'b1;
			else if(ready)
				intStart <= intRestart;

			/* If kernel is done, assert the "done" bit in status register. It clears on read */
			if(done)
				intDone <= 'b1;
			else if(axiARVALID && axiARREADY && 'h00 == axiARADDR)
				intDone <= 'b0;

			/* 0x00: status register */
			if(axiWVALID && axiWREADY && 'h00 == wAddr && axiWSTRB[0])
				intRestart <= axiWDATA[7];

			/* 0x10: LSB of "log" base address */
			if(axiWVALID && axiWREADY && 'h10 == wAddr)
				intOffset[31:0] <= (axiWDATA & wMask) | (intOffset[31:0] & ~wMask);

			/* 0x14: MSB of "log" base address */
			if(axiWVALID && axiWREADY && 'h14 == wAddr)
				intOffset[63:32] <= (axiWDATA & wMask) | (intOffset[63:32] & ~wMask);

			/* 0x1C: pipe "p0" content */
			if(axiWVALID && axiWREADY && 'h1C == wAddr)
				intPipe <= (axiWDATA & wMask) | (intPipe & ~wMask);
		end
	end

endmodule
