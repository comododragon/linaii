`timescale 1ns / 1ps

/**
 * ProfCounter RTL kernel
 *
 * This kernel can receive commands from the OpenCL pipe p0 and write timestamps onto a host-specified global memory region.
 *
 * Action                               | Reaction
 * Start this kernel via clEnqueue...() | ProfCounter starts counting clock cycles
 * Send COMM_STAMP via pipe "p0"        | Saves current clock cycle (timestamp) to global memory
 * Send COMM_CHECKPOINT via pipe "p0"   | Saves current clock cycle (timestamp) to global memory along with the checkpoint ID.
 *                                      | COMM_CHECKPOINT is a multi-valued command, ranging from 0x1 to 0xC in current implementation.
 *                                      | Each value corresponds to a checkpoint ID (command 0x1 is ID 0, command 0x2 is ID 1 and so on).
 *                                      | This is useful to keep track in the code of where the timestamp was requested.
 * Send COMM_HOLD via pipe "p0"         | Timestamps are enqueued and only written to global memory after 0x3 is issued,
 *                                      | preventing competition on global memory that could affect the kernel under test.
 *                                      | This command stays valid until a COMM_FINISH is issued.
 * Send COMM_FINISH via pipe "p0"       | Stops ProfCounter execution
 */
module profCounter(
	/* Standard pins */
	ap_clk,
	ap_rst_n,

	/* AXI4 Master to global memory */
	m_axi_gmem_AWVALID,
	m_axi_gmem_AWREADY,
	m_axi_gmem_AWADDR,
	m_axi_gmem_AWLEN,
	m_axi_gmem_AWSIZE,
	m_axi_gmem_WVALID,
	m_axi_gmem_WREADY,
	m_axi_gmem_WDATA,
	m_axi_gmem_WSTRB,
	m_axi_gmem_WLAST,
	m_axi_gmem_BVALID,
	m_axi_gmem_BREADY,
	m_axi_gmem_BRESP,
	// Unused AXI4 pins
	m_axi_gmem_AWID,
	m_axi_gmem_AWBURST,
	m_axi_gmem_AWLOCK,
	m_axi_gmem_AWCACHE,
	m_axi_gmem_AWPROT,
	m_axi_gmem_AWQOS,
	m_axi_gmem_AWREGION,
	m_axi_gmem_ARVALID,
	m_axi_gmem_ARREADY,
	m_axi_gmem_ARADDR,
	m_axi_gmem_ARID,
	m_axi_gmem_ARLEN,
	m_axi_gmem_ARSIZE,
	m_axi_gmem_ARBURST,
	m_axi_gmem_ARLOCK,
	m_axi_gmem_ARCACHE,
	m_axi_gmem_ARPROT,
	m_axi_gmem_ARQOS,
	m_axi_gmem_ARREGION,
	m_axi_gmem_RVALID,
	m_axi_gmem_RREADY,
	m_axi_gmem_RDATA,
	m_axi_gmem_RLAST,
	m_axi_gmem_RID,
	m_axi_gmem_RRESP,
	m_axi_gmem_BID,

	/* AXI4 Slave to OpenCL kernel controller */
	s_axi_control_AWVALID,
	s_axi_control_AWREADY,
	s_axi_control_AWADDR,
	s_axi_control_WVALID,
	s_axi_control_WREADY,
	s_axi_control_WDATA,
	s_axi_control_WSTRB,
	s_axi_control_ARVALID,
	s_axi_control_ARREADY,
	s_axi_control_ARADDR,
	s_axi_control_RVALID,
	s_axi_control_RREADY,
	s_axi_control_RDATA,
	s_axi_control_RRESP,
	s_axi_control_BVALID,
	s_axi_control_BREADY,
	s_axi_control_BRESP,

	/* AXI4-Stream pipe sink */
	p0_TDATA,
	p0_TVALID,
	p0_TREADY
);

	/* Standard pins */
	input ap_clk;
	input ap_rst_n;

	/* AXI4 Master to global memory */
	output m_axi_gmem_AWVALID;
	input m_axi_gmem_AWREADY;
	output [63:0] m_axi_gmem_AWADDR;
	output [7:0] m_axi_gmem_AWLEN;
	output [2:0] m_axi_gmem_AWSIZE;
	output m_axi_gmem_WVALID;
	input m_axi_gmem_WREADY;
	// XXX: was 32-bit
	output [63:0] m_axi_gmem_WDATA;
	// XXX: was 4-bit
	output [7:0] m_axi_gmem_WSTRB;
	output m_axi_gmem_WLAST;
	input m_axi_gmem_BVALID;
	output m_axi_gmem_BREADY;
	input [1:0] m_axi_gmem_BRESP;
	// Unused AXI4 pins
	output m_axi_gmem_AWID;
	output [1:0] m_axi_gmem_AWBURST;
	output [1:0] m_axi_gmem_AWLOCK;
	output [3:0] m_axi_gmem_AWCACHE;
	output [2:0] m_axi_gmem_AWPROT;
	output [3:0] m_axi_gmem_AWQOS;
	output [3:0] m_axi_gmem_AWREGION;
	output m_axi_gmem_ARVALID;
	input m_axi_gmem_ARREADY;
	output [63:0] m_axi_gmem_ARADDR;
	output m_axi_gmem_ARID;
	output [7:0] m_axi_gmem_ARLEN;
	output [2:0] m_axi_gmem_ARSIZE;
	output [1:0] m_axi_gmem_ARBURST;
	output [1:0] m_axi_gmem_ARLOCK;
	output [3:0] m_axi_gmem_ARCACHE;
	output [2:0] m_axi_gmem_ARPROT;
	output [3:0] m_axi_gmem_ARQOS;
	output [3:0] m_axi_gmem_ARREGION;
	input m_axi_gmem_RVALID;
	output m_axi_gmem_RREADY;
	// XXX: was 32-bit
	input [63:0] m_axi_gmem_RDATA;
	input m_axi_gmem_RLAST;
	input m_axi_gmem_RID;
	input [1:0] m_axi_gmem_RRESP;
	input m_axi_gmem_BID;

	/* AXI4 Slave to OpenCL kernel controller */
	input s_axi_control_AWVALID;
	output s_axi_control_AWREADY;
	input [5:0] s_axi_control_AWADDR;
	input s_axi_control_WVALID;
	output s_axi_control_WREADY;
	input [31:0] s_axi_control_WDATA;
	input [3:0] s_axi_control_WSTRB;
	input s_axi_control_ARVALID;
	output s_axi_control_ARREADY;
	input [5:0] s_axi_control_ARADDR;
	output s_axi_control_RVALID;
	input s_axi_control_RREADY;
	output [31:0] s_axi_control_RDATA;
	output [1:0] s_axi_control_RRESP;
	output s_axi_control_BVALID;
	input s_axi_control_BREADY;
	output [1:0] s_axi_control_BRESP;

	/* AXI4-Stream pipe sink */
	input [31:0] p0_TDATA;
	input p0_TVALID;
	output p0_TREADY;

	/* Registered reset */
	reg ap_rst_n_registered;

	/* Asserted when this module is idle/ready */
	wire profCounterDoneReady;
	/* basicController I/Os */
	wire controlStart;
	reg controlStartRegistered;
	wire controlStartPulse;
	reg controlIdle;
	wire [63:0] controlOffset;
	/* commandUnit I/Os */
	wire commanderDone;
	wire [3:0] commanderOut;
	/* timestamper I/Os */
	wire stamperDone;
	wire [63:0] stamperOut;
	/* sequentialWriter I/Os */
	wire writerIdle;

	/* Unused AXI4 pins, set to neutral values */
	assign m_axi_gmem_AWID = 1'b0;
	assign m_axi_gmem_AWBURST = 2'b01;
	assign m_axi_gmem_AWLOCK = 2'b00;
	assign m_axi_gmem_AWCACHE = 4'b0011;
	assign m_axi_gmem_AWPROT = 3'b000;
	assign m_axi_gmem_AWQOS = 4'b0000;
	assign m_axi_gmem_AWREGION = 4'b0000;
	assign m_axi_gmem_ARVALID = 1'b0;
	assign m_axi_gmem_ARADDR = 'h0;
	assign m_axi_gmem_ARID = 1'b0;
	assign m_axi_gmem_ARLEN = 8'b00000000;
	assign m_axi_gmem_ARSIZE = 3'b000;
	assign m_axi_gmem_ARBURST = 2'b01;
	assign m_axi_gmem_ARLOCK = 2'b00;
	assign m_axi_gmem_ARCACHE = 4'b0011;
	assign m_axi_gmem_ARPROT = 3'b000;
	assign m_axi_gmem_ARQOS = 4'b0000;
	assign m_axi_gmem_ARREGION = 4'b0000;
	assign m_axi_gmem_RREADY = 1'b0;

	assign profCounterDoneReady = writerIdle && commanderDone && stamperDone && !controlStartPulse;
	assign controlStartPulse = controlStart && !controlStartRegistered;

	/* Register reset */
	always @(posedge ap_clk) begin
		ap_rst_n_registered = ap_rst_n;
	end

	/* Register start signal */
	always @(posedge ap_clk) begin
		controlStartRegistered <= controlStart;
	end

	/* Logic to handle the global idle signal */
	always @(posedge ap_clk) begin
		if(!ap_rst_n_registered) begin
			controlIdle <= 1'b1;
		end
		else begin
			if(profCounterDoneReady)
				controlIdle <= 1'b1;
			else if(controlStartPulse)
				controlIdle <= 1'b0;
		end
	end

	BasicController controller(
		.clk(ap_clk),
		.rst_n(ap_rst_n_registered),

		.axiAWADDR(s_axi_control_AWADDR),
		.axiAWVALID(s_axi_control_AWVALID),
		.axiAWREADY(s_axi_control_AWREADY),
		.axiWDATA(s_axi_control_WDATA),
		.axiWSTRB(s_axi_control_WSTRB),
		.axiWVALID(s_axi_control_WVALID),
		.axiWREADY(s_axi_control_WREADY),
		.axiBRESP(s_axi_control_BRESP),
		.axiBVALID(s_axi_control_BVALID),
		.axiBREADY(s_axi_control_BREADY),

		.axiARADDR(s_axi_control_ARADDR),
		.axiARVALID(s_axi_control_ARVALID),
		.axiARREADY(s_axi_control_ARREADY),
		.axiRDATA(s_axi_control_RDATA),
		.axiRRESP(s_axi_control_RRESP),
		.axiRVALID(s_axi_control_RVALID),
		.axiRREADY(s_axi_control_RREADY),

		.start(controlStart),
		.done(profCounterDoneReady),
		.ready(profCounterDoneReady),
		.idle(controlIdle),
		.offset(controlOffset)
	);

	CommandUnit commander(
		.clk(ap_clk),
		.rst_n(ap_rst_n_registered),

		.start(controlStartPulse),
		.done(commanderDone),

		.pipeTDATA(p0_TDATA),
		.pipeTVALID(p0_TVALID),
		.pipeTREADY(p0_TREADY),

		.command(commanderOut)
	);

	Timestamper stamper(
		.clk(ap_clk),
		.rst_n(ap_rst_n_registered),

		.start(controlStartPulse),
		.done(stamperDone),
		.command(commanderOut),
		.timestamp(stamperOut)
	);

	SequentialWriter writer(
		.clk(ap_clk),
		.rst_n(ap_rst_n_registered),

		.offset(controlOffset),
		.command(commanderOut),
		.value(stamperOut),
		.idle(writerIdle),

		.axiAWVALID(m_axi_gmem_AWVALID),
		.axiAWREADY(m_axi_gmem_AWREADY),
		.axiAWADDR(m_axi_gmem_AWADDR),
		.axiAWLEN(m_axi_gmem_AWLEN),
		.axiAWSIZE(m_axi_gmem_AWSIZE),

		.axiWVALID(m_axi_gmem_WVALID),
		.axiWREADY(m_axi_gmem_WREADY),
		.axiWDATA(m_axi_gmem_WDATA),
		.axiWSTRB(m_axi_gmem_WSTRB),
		.axiWLAST(m_axi_gmem_WLAST),

		.axiBRESP(m_axi_gmem_BRESP),
		.axiBVALID(m_axi_gmem_BVALID),
		.axiBREADY(m_axi_gmem_BREADY)
	);

endmodule
