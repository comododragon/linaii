#!/usr/bin/env python3


import os, re, subprocess, sys, shutil


# GLOBAL SWITCHES
bypassTrace = True


vivaiName = os.path.basename(os.path.splitext(__file__)[0])


codingConverter = {
	"uncertainty": {
		0: ("0", "27% uncertainty"),
		1: ("1", "12.5% uncertainty")
	},
	"array": {
		0: ("0", "no array partitioning"),
		1: ("1", "array partitioning configuration 1 (factor 4)")
	},
	"pipelining": {
		0: ("0", "no pipelining"),
		1: ("1", "pipelining in inner loop")
	},
	"unrolling": {
		0: ("00", "no unroll"),
		1: ("01", "unroll in inner loop"),
		2: ("10", "unroll in outer loop"),
		3: ("11", "combination of 1 and 2")
	}
}


kernels = [
	"atax",
	"bicg",
	"convolution2d",
	"convolution3d",
	"gemm",
	"gesummv",
	"mvt",
	"syr2k",
	"syrk"
]


targetLoops = {
	"atax": 1,
	"bicg": 2,
	"convolution2d": 1,
	"convolution3d": 1,
	"gemm": 1,
	"gesummv": 1,
	"mvt": 1,
	"syr2k": 1,
	"syrk": 1
}


arrayConfigs = {
	"atax": [
		("A", "cyclic", 4),
		("x", "complete", 0),
		("tmp", "complete", 0)
	],
	"bicg": [
		("A", "cyclic", 4),
		("r", "complete", 0),
		("s", "complete", 0),
		("p", "complete", 0),
		("q", "complete", 0)
	],
	"convolution2d": [
		("A", "cyclic", 4),
		("B", "cyclic", 4)
	],
	"convolution3d": [
		("A", "cyclic", 4),
		("B", "cyclic", 4)
	],
	"gemm": [
		("A", "cyclic", 4),
		("B", "cyclic", 4),
		("C", "cyclic", 4)
	],
	"gesummv": [
		("A", "cyclic", 4),
		("B", "cyclic", 4),
		("x", "complete", 0),
		("y", "complete", 0),
		("tmp", "complete", 0)
	],
	"mvt": [
		("a", "cyclic", 4),
		("x", "complete", 4),
		("y", "complete", 0)
	],
	"syr2k": [
		("A", "cyclic", 4),
		("B", "cyclic", 4),
		("C", "cyclic", 4)
	],
	"syrk": [
		("A", "cyclic", 4),
		("C", "cyclic", 4)
	]
}


parts = [
	"xczu9eg-ffvb1156-2-e"
]


uncertainties = {
	"0": "27.0",
	"1": "12.5"
}


projectsRoot = os.path.join("baseFiles", "vivadoprojs")
makefilesRoot = os.path.join("baseFiles", "makefiles", "vivado")
workspaceRoot = os.path.join("workspace", "vivado")
csvsRoot = "csvs"
tempFile = "temp.tmp"


def printUsage():
	usageStr = (
		'VIVAI: a script to compute a DSE point for all EcoBench kernels using Vivado\n'
		'\n'
		'Usage: {} UNC ARR PIP UNR [KERNEL]\n'
	).format(vaiName)

	usageStr += "UNC:\n"
	for i in codingConverter["uncertainty"]:
		usageStr += "\t{}: {}\n".format(i, codingConverter["uncertainty"][i][1])
	usageStr += "ARR:\n"
	for i in codingConverter["array"]:
		usageStr += "\t{}: {}\n".format(i, codingConverter["array"][i][1])
	usageStr += "PIP:\n"
	for i in codingConverter["pipelining"]:
		usageStr += "\t{}: {}\n".format(i, codingConverter["pipelining"][i][1])
	usageStr += "UNR:\n"
	for i in codingConverter["unrolling"]:
		usageStr += "\t{}: {}\n".format(i, codingConverter["unrolling"][i][1])

	usageStr += "KERNEL: kernel name to profile (optional)\n"

	sys.stderr.write(usageStr)


def makeMakefile(kernel):
	makefileTokens = {}

	makefileTokens["<KERN>"] = kernel

	# Create adapted Makefile from template
	with open(os.path.join(workspaceRoot, kernel, "Makefile"), "w") as outFile:
		with open(os.path.join(makefilesRoot, "Makefile"), "r") as inFile:
			for l in inFile:
				for tok in makefileTokens:
					l = l.replace(tok, makefileTokens[tok])
				outFile.write(l)


def makeScript(part, kernel, uncertainty):
	scriptTokens = {}

	scriptTokens["<KERN>"] = kernel
	scriptTokens["<PART>"] = part
	scriptTokens["<UNC>"] = uncertainties[uncertainty]

	with open(os.path.join(workspaceRoot, kernel, "script.tcl"), "w") as outFile:
		with open(os.path.join(makefilesRoot, "script.tcl"), "r") as inFile:
			for l in inFile:
				for tok in scriptTokens:
					l = l.replace(tok, scriptTokens[tok])
				outFile.write(l)


def makeSource(kernel, array, pipelining, unrolling):
	sourceTokens = {}

	sourceTokens["<PRAGMA>"] = ""
	if("0" == array):
		pass
	else:
		for i in arrayConfigs[kernel]:
			if "complete" == i[1]:
				sourceTokens["<PRAGMA>"] += "#pragma HLS array_partition variable={} complete\n".format(i[0])
			else:
				sourceTokens["<PRAGMA>"] += "#pragma HLS array_partition variable={} {} factor={}\n".format(i[0], i[1], i[2])

	sourceTokens["<PRAGMA2>"] = ""
	sourceTokens["<PRAGMA3>"] = ""
	if("0" == pipelining):
		pass
	else:
		sourceTokens["<PRAGMA3>"] += "#pragma HLS pipeline\n"

	if("00" == unrolling):
		pass
	elif("01" == unrolling):
		sourceTokens["<PRAGMA3>"] += "#pragma HLS unroll factor=2\n"
	elif("10" == unrolling):
		sourceTokens["<PRAGMA2>"] += "#pragma HLS unroll factor=2\n"
	else:
		sourceTokens["<PRAGMA2>"] += "#pragma HLS unroll factor=2\n"
		sourceTokens["<PRAGMA3>"] += "#pragma HLS unroll factor=2\n"

	with open(os.path.join(workspaceRoot, kernel, "src", "{}.cpp".format(kernel)), "w") as outFile:
		with open(os.path.join(projectsRoot, kernel, "src", "{}.cpp".format(kernel)), "r") as inFile:
			for l in inFile:
				for tok in sourceTokens:
					l = l.replace(tok, sourceTokens[tok])
				outFile.write(l)


if "__main__" == __name__:
	if len(sys.argv) < 5:
		printUsage()
		exit(1)

	unc = int(sys.argv[1])
	arr = int(sys.argv[2])
	pip = int(sys.argv[3])
	unr = int(sys.argv[4])
	kern = sys.argv[5] if len(sys.argv) > 5 else None

	if unc not in codingConverter["uncertainty"] or arr not in codingConverter["array"] or pip not in codingConverter["pipelining"] or unr not in codingConverter["unrolling"]:
		printUsage()
		exit(1)

	if kern is not None and kern not in kernels:
		printUsage()
		exit(1)

	print("Uncertainty: {}".format(codingConverter["uncertainty"][unc][1]))
	print("Array partitioning: {}".format(codingConverter["array"][arr][1]))
	print("Pipelining: {}".format(codingConverter["pipelining"][pip][1]))
	print("Unrolling: {}".format(codingConverter["unrolling"][unr][1]))
	if kern is not None:
		print("Kernel: {}".format(kern))

	unc = codingConverter["uncertainty"][unc][0]
	arr = codingConverter["array"][arr][0]
	pip = codingConverter["pipelining"][pip][0]
	unr = codingConverter["unrolling"][unr][0]

	code = "unc{}_arr{}_pip{}_unr{}".format(unc, arr, pip, unr)
	work = kernels if kern is None else [kern]
	resultString = ""

	try:
		for k in work:
			# Delete workspace (if present)
			try:
				shutil.rmtree(os.path.join(workspaceRoot, k))
			except FileNotFoundError:
				pass

			# Copy project
			shutil.copytree(os.path.join(projectsRoot, k), os.path.join(workspaceRoot, k))

		for p in parts:
			for k in work:
				# Generate regexes
				topLoopMatcher = re.compile(r"\s*\|- Loop {}\s*\|\s*(\d*)\s*\|\s*(\d*)\s*\|\s*(\d*)\s*\|\s*(.*)\s*\|\s*(.*)\s*\|\s*(\d*)\s*\|\s*(.*)\s*\|".format(targetLoops[k]))
				innerLoopMatcher = re.compile(r"\s*\|\s+(.*)\s*\|\s*(\d*)\s*\|\s*(\d*)\s*\|\s*(\d*)\s*\|\s*(.*)\s*\|\s*(.*)\s*\|\s*(\d*)\s*\|\s*(.*)\s*\|")

				# Generate customised Makefile
				makeMakefile(k)

				# Generate vivado script
				makeScript(p, k, unc)

				# Generate source file
				makeSource(k, arr, pip, unr)

				# Run make
				result = subprocess.run(["make", "vivado"], cwd=os.path.join(workspaceRoot, k))

				with open(os.path.join(workspaceRoot, k, k, "solution1", "syn", "report", "{}_csynth.rpt".format(k)), "r") as rpt:
					topLoopFound = False
					latency = ""
					il = ""
					ii = ""
					for l in rpt:
						topFound = topLoopMatcher.match(l)
						if topFound is not None:
							latency = str(topFound.group(2))
							il = str(topFound.group(3))
							ii = str(topFound.group(4))
							topLoopFound = True
						elif topLoopFound:
							innerFound = innerLoopMatcher.match(l)

							if innerFound is not None:
								# This may execute many times, but it's fine, we are only interested in the last run
								il = str(innerFound.group(4))
								ii = str(innerFound.group(5))
							else:
								break

				resultString += "{},{},{}\n".format(latency, il, ii)

			resultString += "\n"

		with open(os.path.join(csvsRoot, "{}_viv.csv".format(code)), "w") as out:
			out.write(resultString)
	finally:
		pass
		# Clean workspace
		#for k in kernels:
		#	try:
		#		shutil.rmtree(os.path.join(workspaceRoot, k))
		#	except FileNotFoundError:
		#		pass
