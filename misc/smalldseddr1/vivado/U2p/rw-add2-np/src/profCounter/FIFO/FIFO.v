`define CLOG2(x) (\
	(x <= 2)? 1 :\
	(x <= 4)? 2 :\
	(x <= 8)? 3 :\
	(x <= 16)? 4 :\
	(x <= 32)? 5 :\
	(x <= 64)? 6 :\
	(x <= 128)? 7 :\
	(x <= 256)? 8 :\
	(x <= 512)? 9 :\
	(x <= 1024)? 10 :\
	-1\
)

module FIFO#(
	parameter SIZE = 16,
	parameter DATA_WIDTH = 32
) (
	/* Standard pins */
	clk,
	rst_n,

	/* FIFO commands */
	enqueue,
	dequeue,
	/* FIFO input */
	back,
	/* FIFO output */
	front,
	/* Status signals */
	full,
	empty
);

	input clk;
	input rst_n;

	input enqueue;
	input dequeue;
	input [DATA_WIDTH-1:0] back;
	output [DATA_WIDTH-1:0] front;
	output full;
	output empty;

	reg [`CLOG2(SIZE)-1:0] backPointer;
	reg [`CLOG2(SIZE)-1:0] frontPointer;
	reg [`CLOG2(SIZE):0] occupied;
	reg rawDetected;
	reg [DATA_WIDTH-1:0] rawReg;
	wire [DATA_WIDTH-1:0] ramReadData;
	wire commitEnqueue;
	wire commitDequeue;
	wire [`CLOG2(SIZE)-1:0] frontPointerNow;

	assign front = rawDetected? rawReg : ramReadData;
	assign full = SIZE == occupied;
	assign empty = 'h0 == occupied;

	/* Enqueue only happens when FIFO is not full */
	assign commitEnqueue = enqueue && !full;
	/* Dequeue only happens when FIFO is not empty */
	assign commitDequeue = dequeue && !empty;
	/* The "now" signal masks the registered output latency from the RAM */
	assign frontPointerNow = commitDequeue? (((SIZE - 'h1) == frontPointer)? 'h0 : (frontPointer + 'h1)) : frontPointer;

	/* Simple dual-port synchronous memory */
	SyncRAMSimpleDualPort#(`CLOG2(SIZE), DATA_WIDTH) ram(
		.clk(clk),

		/* Port A is FIFO input */
		.enA(1'b1),
		.writeA(commitEnqueue),
		.addressA(backPointer),
		.writeDataA(back),

		/* Port B is FIFO output */
		.enB(1'b1),
		.addressB(frontPointerNow),
		.readDataB(ramReadData)
	);

	always @(posedge clk) begin
		if(!rst_n) begin
			backPointer <= 'h0;
			frontPointer <= 'h0;
			occupied <= 'h0;
			rawDetected <= 1'b0;
			rawReg <= 'h0;
		end
		else begin
			/* Update pointers */
			if(commitEnqueue)
				backPointer <= ((SIZE - 'h1) == backPointer)? 'h0 : (backPointer + 'h1);
			if(commitDequeue)
				frontPointer <= frontPointerNow;

			/* Occupied does not change if nothing happens or if simultaneous enqueue/dequeue happens */
			if(commitEnqueue && !commitDequeue)
				occupied <= occupied + 'h1;
			else if(!commitEnqueue && commitDequeue)
				occupied <= occupied - 'h1;

			/* We want the memory to work as a read-after-write logic (e.g. when addressA and addressB matches) */
			/* Thus when this happens, we save this state, so that the next clock the output will be concise */
			if(commitEnqueue && (frontPointerNow == backPointer)) begin
				rawDetected <= 1'b1;
				rawReg <= back;
			end
			else begin
				rawDetected <= 1'b0;
			end
		end
	end

endmodule
