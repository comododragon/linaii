#!/usr/bin/env python3


import os, time, subprocess, sys, shutil


# GLOBAL SWITCHES
bypassTrace = True


vaiName = os.path.basename(os.path.splitext(__file__)[0])


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


versions = [
	"2_updlat",
	"7_npla"
]


paths = {
	"2_updlat": "/home/perina/Desktop/praAcabar/2_updlat/build/bin",
	"7_npla": "/home/perina/Desktop/praAcabar/7_npla/build/bin"
}


configScheme = {
	"atax": {
		"arrays": [
			("A", 65536, 4),
			("x", 512, 4),
			("tmp", 512, 4)
		],
		"partitioning": {
			"0": [],
			"1": [
				("cyclic", 4),
				("complete", 0),
				("complete", 0)
			]
		},
		"pipelining": {
			"0": [],
			"1": [(0, 2)]
		},
		"unrolling": {
			"00": [],
			"01": [(0, 2, 7, 2)],
			"10": [(0, 1, 4, 2)],
			"11": [(0, 2, 7, 2), (0, 1, 4, 2)]
		}
	},
	"bicg": {
		"arrays": [
			("A", 262144, 4),
			("r", 1024, 4),
			("s", 1024, 4),
			("p", 1024, 4),
			("q", 1024, 4)
		],
		"partitioning": {
			"0": [],
			"1": [
				("cyclic", 4),
				("complete", 0),
				("complete", 0),
				("complete", 0),
				("complete", 0)
			]
		},
		"pipelining": {
			"0": [],
			"1": [(0, 2)]
		},
		"unrolling": {
			"00": [],
			"01": [(0, 2, 10, 2)],
			"10": [(0, 1, 7, 2)],
			"11": [(0, 2, 10, 2), (0, 1, 7, 2)]
		}
	},
	"convolution2d": {
		"arrays": [
			("A", 65536, 4),
			("B", 65536, 4)
		],
		"partitioning": {
			"0": [],
			"1": [
				("cyclic", 4),
				("cyclic", 4)
			]
		},
		"pipelining": {
			"0": [],
			"1": [(0, 2)]
		},
		"unrolling": {
			"00": [],
			"01": [(0, 2, 10, 2)],
			"10": [(0, 1, 9, 2)],
			"11": [(0, 2, 10, 2), (0, 1, 9, 2)]
		}
	},
	"convolution3d": {
		"arrays": [
			("A", 131072, 4),
			("B", 131072, 4)
		],
		"partitioning": {
			"0": [],
			"1": [
				("cyclic", 4),
				("cyclic", 4)
			]
		},
		"pipelining": {
			"0": [],
			"1": [(0, 3)]
		},
		"unrolling": {
			"00": [],
			"01": [(0, 3, 11, 2)],
			"10": [(0, 2, 10, 2)],
			"11": [(0, 3, 11, 2), (0, 2, 10, 2)]
		}
	},
	"gemm": {
		"arrays": [
			("A", 65536, 4),
			("B", 65536, 4),
			("C", 65536, 4)
		],
		"partitioning": {
			"0": [],
			"1": [
				("cyclic", 4),
				("cyclic", 4),
				("cyclic", 4)
			]
		},
		"pipelining": {
			"0": [],
			"1": [(0, 3)]
		},
		"unrolling": {
			"00": [],
			"01": [(0, 3, 8, 2)],
			"10": [(0, 2, 5, 2)],
			"11": [(0, 3, 8, 2), (0, 2, 5, 2)]
		}
	},
	"gesummv": {
		"arrays": [
			("A", 65536, 4),
			("B", 65536, 4),
			("x", 512, 4),
			("y", 512, 4),
			("tmp", 512, 4)
		],
		"partitioning": {
			"0": [],
			"1": [
				("cyclic", 4),
				("cyclic", 4),
				("complete", 0),
				("complete", 0),
				("complete", 0)
			]
		},
		"pipelining": {
			"0": [],
			"1": [(0, 2)]
		},
		"unrolling": {
			"00": [],
			"01": [(0, 2, 8, 2)],
			"10": [(0, 1, 4, 2)],
			"11": [(0, 2, 8, 2), (0, 1, 4, 2)]
		}
	},
	"mvt": {
		"arrays": [
			("a", 262144, 4),
			("x", 1024, 4),
			("y", 1024, 4)
		],
		"partitioning": {
			"0": [],
			"1": [
				("cyclic", 4),
				("complete", 0),
				("complete", 0)
			]
		},
		"pipelining": {
			"0": [],
			"1": [(0, 2)]
		},
		"unrolling": {
			"00": [],
			"01": [(0, 2, 5, 2)],
			"10": [(0, 1, 4, 2)],
			"11": [(0, 2, 5, 2), (0, 1, 4, 2)]
		}
	},
	"syr2k": {
		"arrays": [
			("A", 65536, 4),
			("B", 65536, 4),
			("C", 65536, 4)
		],
		"partitioning": {
			"0": [],
			"1": [
				("cyclic", 4),
				("cyclic", 4),
				("cyclic", 4)
			]
		},
		"pipelining": {
			"0": [],
			"1": [(0, 3)]
		},
		"unrolling": {
			"00": [],
			"01": [(0, 3, 8, 2)],
			"10": [(0, 2, 5, 2)],
			"11": [(0, 3, 8, 2), (0, 2, 5, 2)]
		}
	},
	"syrk": {
		"arrays": [
			("A", 65536, 4),
			("C", 65536, 4)
		],
		"partitioning": {
			"0": [],
			"1": [
				("cyclic", 4),
				("cyclic", 4)
			]
		},
		"pipelining": {
			"0": [],
			"1": [(0, 3)]
		},
		"unrolling": {
			"00": [],
			"01": [(0, 3, 8, 2)],
			"10": [(0, 2, 5, 2)],
			"11": [(0, 3, 8, 2), (0, 2, 5, 2)]
		}
	}
}


projectsRoot = os.path.join("baseFiles", "projects")
makefilesRoot = os.path.join("baseFiles", "makefiles")
tracesRoot = os.path.join("baseFiles", "traces")
parsersRoot = os.path.join("baseFiles", "parsers")
workspaceRoot = os.path.join("workspace", "lin-a")
csvsRoot = "csvs"
tempFile = "temp.tmp"


def printUsage():
	usageStr = (
		'VAI: a script to compute a DSE point for all EcoBench kernels\n'
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


def makeMakefile(version, kernel, uncertainty, pipelining):
	makefileTokens = {}

	makefileTokens["<KERN>"] = kernel
	makefileTokens["<ARGS>"] = "" if not bypassTrace else "--mode estimation "

	if "0" == pipelining:
		if "2_updlat" == version:
			makefileTokens["<ARGS>"] += "--f-es "

	if "7_npla" == version:
		makefileTokens["<ARGS>"] += "--f-npla "

	if "7_npla" == version:
		if "1" == uncertainty:
			makefileTokens["<ARGS>"] += "-u 12.5 "

	makefileTokens["<ARGS>"] += "-t ZCU102 "

	# Create adapted Makefile from template
	with open(os.path.join(workspaceRoot, kernel, "Makefile"), "w") as outFile:
		with open(os.path.join(makefilesRoot, "lina", "Makefile"), "r") as inFile:
			for l in inFile:
				for tok in makefileTokens:
					l = l.replace(tok, makefileTokens[tok])
				outFile.write(l)


def makeConfig(version, kernel, array, pipelining, unrolling):
	with open(os.path.join(workspaceRoot, kernel, "src", "config.cfg"), "w") as cfg:
		# Write arrays info
		for a in configScheme[kernel]["arrays"]:
			cfg.write("array,{},{},{}\n".format(a[0], a[1], a[2]))

		# Write partitioning info
		for i in range(len(configScheme[kernel]["partitioning"][array])):
			if configScheme[kernel]["partitioning"][array][i] is not None:
				partType = configScheme[kernel]["partitioning"][array][i][0]
				arrayName = configScheme[kernel]["arrays"][i][0]
				arrayTotalSize = configScheme[kernel]["arrays"][i][1]
				arrayWordSize = configScheme[kernel]["arrays"][i][2]
				if "complete" == partType:
					cfg.write("partition,complete,{},{}\n".format(arrayName, arrayTotalSize))
				else:
					partFactor = configScheme[kernel]["partitioning"][array][i][1]
					cfg.write("partition,{},{},{},{},{}\n".format(partType, arrayName, arrayTotalSize, arrayWordSize, partFactor))

		# Write pipelining info
		for p in configScheme[kernel]["pipelining"][pipelining]:
			cfg.write("pipeline,{},{},{}\n".format(kernel, p[0], p[1]))

		# Write unrolling info
		for u in configScheme[kernel]["unrolling"][unrolling]:
			cfg.write("unrolling,{},{},{},{},{}\n".format(kernel, u[0], u[1], u[2], u[3]))


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
	timerString = ""

	try:
		# Prepare all bytecodes. They never change, so we can compile only once and save some time
		for k in work:
			# Delete workspace (if present)
			try:
				shutil.rmtree(os.path.join(workspaceRoot, k))
			except FileNotFoundError:
				pass

			# Copy project
			shutil.copytree(os.path.join(projectsRoot, k), os.path.join(workspaceRoot, k))

			# Generate Makefile. We are not worried here with the optimisations parameters, as we are only going to run the invariant part
			makeMakefile("2_updlat", k, 0, 0)

			# Update PATH
			newEnv = os.environ.copy()
			newEnv["PATH"] = "{}:{}".format(paths["2_updlat"], newEnv["PATH"])

			# Make bytecode
			subprocess.run(["make", "prof/linked_opt.bc"], cwd=os.path.join(workspaceRoot, k), env=newEnv)

		for v in versions:
			timer = 0

			# Update PATH
			newEnv = os.environ.copy()
			newEnv["PATH"] = "{}:{}".format(paths[v], newEnv["PATH"])

			# Make soft-link for the parser
			parserPath = os.path.join("..", "..", parsersRoot, "pip{}".format(pip), "7_npla" if "7_npla" == v else "x_misc")
			subprocess.run(["ln", "-sf", os.path.join(parserPath, "parse.sh"), "."], cwd=os.path.join(workspaceRoot))

			for k in work:
				# Generate customised Makefile
				makeMakefile(v, k, unc, pip)

				# Generate config file
				makeConfig(v, k, arr, pip, unr)

				# Make soft-link for the trace file
				if bypassTrace:
					subprocess.run(["ln", "-sf", os.path.join("..", "..", "..", "..", tracesRoot, k, "dynamic_trace.gz"), "."], cwd=os.path.join(workspaceRoot, k, "prof"))

				# Run lin-analyzer
				then = time.time_ns()
				result = subprocess.run(["make", "profile" if "7_npla" == v else "profile_old"], cwd=os.path.join(workspaceRoot, k), env=newEnv, stderr=subprocess.STDOUT, stdout=subprocess.PIPE).stdout
				timer += time.time_ns() - then

				# Save to temporary file
				with open(os.path.join(workspaceRoot, tempFile), "wb") as f:
					f.write(result)

				# Run parser
				final = subprocess.run(
					[os.path.join(".", workspaceRoot, "parse.sh"), os.path.join(workspaceRoot, tempFile), str(len(configScheme[k]["arrays"]))],
					stderr=subprocess.STDOUT, stdout=subprocess.PIPE
				).stdout

				resultString += final.decode("utf-8")

			resultString += "\n"
			timerString += "{}\n".format(timer)

		with open(os.path.join(csvsRoot, "{}_time.csv".format(code)), "w") as out:
			out.write(timerString)
		with open(os.path.join(csvsRoot, "{}.csv".format(code)), "w") as out:
			out.write(resultString)
	finally:
		pass
		# Clean workspace
		#for k in kernels:
		#	try:
		#		shutil.rmtree(os.path.join(workspaceRoot, k))
		#	except FileNotFoundError:
		#		pass
