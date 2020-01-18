`timescale 1ns / 1ps

module tb;

	reg clk;
	reg rst_n;

	reg enqueue;
	reg dequeue;
	reg [7:0] back;
	wire [7:0] front;
	wire full;
	wire empty;

	/* DUT */
	FIFO#(4, 8) inst(
		.clk(clk),
		.rst_n(rst_n),

		.enqueue(enqueue),
		.dequeue(dequeue),
		.back(back),
		.front(front),
		.full(full),
		.empty(empty)
	);

	initial begin
		$dumpfile("tb.vcd");
		$dumpvars(1, clk, rst_n, enqueue, dequeue, back, front, full, empty);

		/* Reset */
		clk <= 'b1;
		rst_n <= 'b0;
		enqueue <= 'b0;
		dequeue <= 'b0;
		back <= 'h00;
		#200 @(posedge clk);

		/* Release reset */
		rst_n <= 'b1;
		#200 @(posedge clk);

		/* FRONT [  |  |  |  ] BACK */

		/* Enqueue some elements */
		enqueue <= 'b1;
		back <= 'hDE;
		#50 @(posedge clk);
		back <= 'hAD;
		#50 @(posedge clk);

		/* FRONT [DE|AD|  |  ] BACK */

		/* Dequeue one element */
		enqueue <= 'b0;
		dequeue <= 'b1;
		#50 @(posedge clk);

		/* FRONT [AD|  |  |  ] BACK */

		/* Fill the FIFO */
		enqueue <= 'b1;
		dequeue <= 'b0;
		back <= 'hAE;
		#50 @(posedge clk);
		back <= 'hCA;
		#50 @(posedge clk);
		back <= 'hFE;
		#50 @(posedge clk);
		back <= 'hAA;
		#50 @(posedge clk);
		back <= 'hBB;
		#50 @(posedge clk);
		back <= 'hCC;
		#50 @(posedge clk);
		back <= 'hDD;
		#50 @(posedge clk);

		/* FRONT [AD|AE|CA|FE] BACK */

		/* Dequeue all elements */
		enqueue <= 'b0;
		dequeue <= 'b1;
		#400 @(posedge clk);

		/* FRONT [  |  |  |  ] BACK */

		/* Insert one element */
		enqueue <= 'b1;
		dequeue <= 'b0;
		back <= 'hAB;
		#50 @(posedge clk);

		/* FRONT [AB|  |  |  ] BACK */

		/* Simultaneosly enqueue/dequeue */
		enqueue <= 'b1;
		dequeue <= 'b1;
		back <= 'hCD;
		#50 @(posedge clk);

		/* FRONT [CD|  |  |  ] BACK */

		/* Fill the FIFO */
		enqueue <= 'b1;
		dequeue <= 'b0;
		back <= 'hAD;
		#50 @(posedge clk);
		back <= 'hCA;
		#50 @(posedge clk);
		back <= 'hFE;
		#50 @(posedge clk);
		back <= 'hAA;
		#50 @(posedge clk);
		back <= 'hBB;
		#50 @(posedge clk);
		back <= 'hCC;
		#50 @(posedge clk);
		back <= 'hDD;
		#50 @(posedge clk);

		/* FRONT [CD|AD|CA|FE] BACK */

		/* Simultaneosly enqueue/dequeue (enqueue won't work as the FIFO is full) */
		enqueue <= 'b1;
		dequeue <= 'b1;
		back <= 'hEF;
		#50 @(posedge clk);

		/* FRONT [AD|CA|FE|  ] BACK */

		/* Dequeue two elements */
		enqueue <= 'b0;
		dequeue <= 'b1;
		#200 @(posedge clk);

		/* FRONT [FE|  |  |  ] BACK */

		/* Simultaneosly enqueue/dequeue */
		enqueue <= 'b1;
		dequeue <= 'b1;
		back <= 'hAE;
		#50 @(posedge clk);

		/* FRONT [AE|  |  |  ] BACK */

		/* Clean FIFO and finish */
		enqueue <= 'b0;
		dequeue <= 'b1;
		#400 @(posedge clk);

		/* FRONT [  |  |  |  ] BACK */

		$finish;
	end

	always begin
		#50 clk <= ~clk;
	end

endmodule
