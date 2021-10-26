#!/usr/bin/env python3


import json, os, re, shutil, subprocess, sys, time, threading


appName = os.path.splitext(sys.argv[0])[0].replace("./", "")
env = os.environ


class ProgressConsole:
	def __init__(self, main, jobs, *args, **kwargs):
		self._dimensions = shutil.get_terminal_size()

		if self._dimensions[0] > 80:
			self._maxRange = 70
		elif self._dimensions[0] > 50:
			self._maxRange = 40
		else:
			raise RuntimeError("Screen is too small")

		self._textRange = int(0.8 * self._dimensions[0])
		self._jobs = jobs
		self._noOfJobs = len(jobs)
		self._curJobID = 0
		self._truncString = "{{:.{}s}}...".format(self._textRange - 3)
		self._fullString = "{{:{}s}}".format(self._textRange)

		print("=" * (self._dimensions[0] - 1))

		for i in range(self._noOfJobs):
			self.setProgress(i, 0, "", self._jobs[i])

		try:
			sys.stdout.write("\x1b[?25l")

			main(self, *args, **kwargs)
		finally:
			self.moveCursorV((self._noOfJobs - self._curJobID) * 4)
			sys.stdout.write("\x1b[?25h")


	def setCursorPosition(self, row, col):
		sys.stdout.write("\033[{};{}f".format(row, col))

	def moveCursorV(self, offset):
		if offset > 0:
			sys.stdout.write("\033[{}B".format(offset))
		elif offset < 0:
			sys.stdout.write("\033[{}A".format(-offset))

	def setProgress(self, jobID, progress, description, title=None):
		if jobID > self._noOfJobs:
			raise IndexError("Job ID is out of range")

		self.moveCursorV((jobID - self._curJobID) * 4)
		self._curJobID = jobID

		if title is None:
			self.moveCursorV(1)
		else:
			if len(title) >= self._textRange:
				print(self._truncString.format(title))
			else:
				print(self._fullString.format(title))

		print("{:3d}% {}{}".format(progress, "▓" * int(progress * (self._maxRange / 100.0)), "░" * int((100 - progress) * (self._maxRange / 100.0))))

		if description is None:
			self.moveCursorV(1)
		else:
			if len(description) >= self._textRange:
				print(self._truncString.format(description))
			else:
				print(self._fullString.format(description))

		if title is None:
			self.moveCursorV(-3)
		else:
			print("=" * (self._dimensions[0] - 1))
			self.moveCursorV(-4)


class Configurator(object):
	def __init__(self, config):
		self.config = config

		if "periods" in self.config:
			self._freqPerKey = "periods"
		elif "frequencies" in self.config:
			self._freqPerKey = "frequencies"
		else:
			raise KeyError("at least \"periods\" or \"frequencies\" must be supplied")

		self.freqPerCfg = self.config[self._freqPerKey][0]
		self.loopsCfg = []
		self.arraysCfg = []

		for idx, lop in enumerate(self.config["loops"]):
			self.loopsCfg.append({
				"id": idx,
				"depth": 1,
				"unrolling": None,
				"pipelining": False
			})

			depth = 2
			nest = self.config["loops"][idx]["nest"]
			while len(nest) > 0:
				self.loopsCfg.append({
					"id": idx,
					"depth": depth,
					"unrolling": None,
					"pipelining": False
				})

				depth += 1
				nest = nest["nest"]

		for arr in self.config["arrays"]:
			if not ("offchip" in self.config and arr in self.config["offchip"]):
				self.arraysCfg.append({
					"name": arr,
					"type": "none",
					"factor": 0
				})

		self.started = False

	def generateDesignPoint(self):
		if self.started:
			self._generateDesignPoint()
			return self.startDesignPoint != self.getCode()
		else:
			self.startDesignPoint = self.getCode()
			self.started = True
			return True

	def getPeriod(self):
		return self.freqPerCfg if "periods" == self._freqPerKey else 1000.0 / self.freqPerCfg

	def getFrequency(self):
		return self.freqPerCfg if "frequencies" == self._freqPerKey else 1000.0 / self.freqPerCfg

	def getLoops(self):
		return self.loopsCfg

	def getArrays(self):
		return self.arraysCfg

	def getBanking(self):
		return "banking" in self.config and self.config["banking"]

	def getCode(self):
		code = "{}{:.1f}".format("p" if "periods" == self._freqPerKey else "f", self.freqPerCfg)

		for lop in self.loopsCfg:
			code += "_l{}.{}.{}.{}".format(lop["id"], lop["depth"], int(lop["pipelining"]), lop["unrolling"] if lop["unrolling"] is not None else 0)
		for arr in self.arraysCfg:
			code += "_a{}.{}.{}".format(arr["name"], arr["type"], arr["factor"])

		return code

	def bypass(self):
		# Find for pipelined loops
		for lop in self.loopsCfg:
			if lop["pipelining"]:
				# Check if this is a full unroll. If positive, there is no point making pipeline
				loopCfg = self.config["loops"][lop["id"]]
				for i in range(lop["depth"] - 1):
					loopCfg = loopCfg["nest"]
				if lop["unrolling"] is not None and lop["unrolling"] == loopCfg["bound"]:
					return True

				# Now look for nested loops where pipelining or unroll are active
				for slop in self.loopsCfg:
					# Found a nested loop
					if slop["id"] == lop["id"] and slop["depth"] > lop["depth"]:
						# If pipelining or unrolling are active here, this design point is invalid,
						# since pipelining was already activated for an upper loop
						if slop["pipelining"] or slop["unrolling"] is not None:
							return True

		return False

	def _generateDesignPoint(self):
		# Alternate unrolling bits
		for lop in self.loopsCfg:
			loopCfg = self.config["loops"][lop["id"]]
			for i in range(lop["depth"] - 1):
				loopCfg = loopCfg["nest"]

			if lop["unrolling"] is None:
				if len(loopCfg["unrolling"]) != 0:
					lop["unrolling"] = loopCfg["unrolling"][0]
					return
			else:
				curIdx = loopCfg["unrolling"].index(lop["unrolling"])

				if len(loopCfg["unrolling"]) == curIdx + 1:
					lop["unrolling"] = None
				else:
					lop["unrolling"] = loopCfg["unrolling"][curIdx + 1]
					return

		# Alternate pipelining bits
		for lop in self.loopsCfg:
			loopCfg = self.config["loops"][lop["id"]]
			for i in range(lop["depth"] - 1):
				loopCfg = loopCfg["nest"]

			if not lop["pipelining"]:
				if loopCfg["pipelining"]:
					lop["pipelining"] = True
					return
			else:
				lop["pipelining"] = False

		# Alternate array bits
		for arr in self.arraysCfg:
			if "none" == arr["type"]:
				if len(self.config["arrays"][arr["name"]]["block"]) != 0:
					arr["type"] = "block"
					arr["factor"] = self.config["arrays"][arr["name"]]["block"][0]
					return
				elif len(self.config["arrays"][arr["name"]]["cyclic"]) != 0:
					arr["type"] = "cyclic"
					arr["factor"] = self.config["arrays"][arr["name"]]["cyclic"][0]
					return
				elif self.config["arrays"][arr["name"]]["complete"]:
					arr["type"] = "complete"
					arr["factor"] = 0
					return
			elif "block" == arr["type"]:
				curIdx = self.config["arrays"][arr["name"]]["block"].index(arr["factor"])

				if len(self.config["arrays"][arr["name"]]["block"]) == curIdx + 1:
					if len(self.config["arrays"][arr["name"]]["cyclic"]) != 0:
						arr["type"] = "cyclic"
						arr["factor"] = self.config["arrays"][arr["name"]]["cyclic"][0]
						return
					elif self.config["arrays"][arr["name"]]["complete"]:
						arr["type"] = "complete"
						arr["factor"] = 0
						return
					else:
						arr["type"] = "none"
						arr["factor"] = 0
				else:
					arr["factor"] = self.config["arrays"][arr["name"]]["block"][curIdx + 1]
					return
			elif "cyclic" == arr["type"]:
				curIdx = self.config["arrays"][arr["name"]]["cyclic"].index(arr["factor"])

				if len(self.config["arrays"][arr["name"]]["cyclic"]) == curIdx + 1:
					if self.config["arrays"][arr["name"]]["complete"]:
						arr["type"] = "complete"
						arr["factor"] = 0
						return
					else:
						arr["type"] = "none"
						arr["factor"] = 0
				else:
					arr["factor"] = self.config["arrays"][arr["name"]]["cyclic"][curIdx + 1]
					return
			elif "complete" == arr["type"]:
				arr["type"] = "none"
				arr["factor"] = 0

		# Alternate period bits
		curIdx = self.config[self._freqPerKey].index(self.freqPerCfg)

		if len(self.config[self._freqPerKey]) == curIdx + 1:
			self.freqPerCfg = self.config[self._freqPerKey][0]
		else:
			self.freqPerCfg = self.config[self._freqPerKey][curIdx + 1]
			return

	def patch(baseCfg, patchCfg):
		for key in patchCfg:
			if key != "inherits":
				baseCfg[key] = patchCfg[key]


def parseJsonFiles(experiment, k):
	baseExpRelPath = ""

	with open(os.path.join("sources", experiment, k, "{}.json".format(k)), "r") as jsonF:
		jsonContent = json.loads(jsonF.read())

	if "inherits" in jsonContent and jsonContent["inherits"] is not None and jsonContent["inherits"] != "":
		patchJsonContent = dict(jsonContent)
		# Generate relative path from "inherits" value
		baseExpRelPath = os.path.join(*([".."] * (jsonContent["inherits"].count(os.path.sep) + 1)), jsonContent["inherits"])

		with open(os.path.join("sources", experiment, k, baseExpRelPath, "{}.json".format(k)), "r") as jsonF:
			jsonContent = json.loads(jsonF.read())
			Configurator.patch(jsonContent, patchJsonContent)

	return jsonContent, baseExpRelPath


def generate(console, options, experiment, kernels):
	if not os.path.exists("workspace"):
		os.mkdir("workspace")
	if not os.path.exists(os.path.join("workspace", experiment)):
		os.mkdir(os.path.join("workspace", experiment))

	modEnv = os.environ
	if options["PATH"][1] is not None:
		modEnv["PATH"] = "{}:{}".format(options["PATH"][1], modEnv["PATH"])

	for k in kernels:
		if os.path.exists(os.path.join("workspace", experiment, k)):
			raise FileExistsError("Folder for kernel \"{}\" (experiment \"{}\") exists".format(k, experiment))
		else:
			os.mkdir(os.path.join("workspace", experiment, k))

		# Read the json config files (following inheritance)
		jsonContent, baseExpRelPath = parseJsonFiles(experiment, k)

		console.setProgress(0, 0, "Preparing...", "Enumerating design points for {}".format(k))
		cfgEnum = Configurator(jsonContent)
		noOfPoints = 0

		while cfgEnum.generateDesignPoint():
			console.setProgress(0, 0, "{} points and counting...".format(noOfPoints))
			noOfPoints += 1

		console.setProgress(0, 0, "{} points counted".format(noOfPoints))

		console.setProgress(0, 0, "Preparing...", "Generating design points for {}".format(k))
		cfgGen = Configurator(jsonContent)
		i = 0

		console.setProgress(0, 0, "Generating base folder...")
		if os.path.exists(os.path.join("workspace", experiment, k, "base")):
			raise FileExistsError("Base folder (kernel \"{}\", experiment \"{}\") exists".format(k, experiment))
		else:
			os.mkdir(os.path.join("workspace", experiment, k, "base"))
		shutil.copytree(
			os.path.join("sources", experiment, k, baseExpRelPath, "lina"),
			os.path.join("workspace", experiment, k, "base", "lina"),
			symlinks=True
		)
		os.symlink(
			os.path.join("..", "..", "..", "..", "sources", experiment, k, baseExpRelPath, "Makefile"),
			os.path.join("workspace", experiment, k, "base", "Makefile")
		)

		while cfgGen.generateDesignPoint():
			code = cfgGen.getCode()
			console.setProgress(0, int(100 * (i / noOfPoints)), code)
			i += 1

			if cfgGen.bypass():
				continue

			if os.path.exists(os.path.join("workspace", experiment, k, code)):
				raise FileExistsError("Folder for code \"{}\" (kernel \"{}\", experiment \"{}\") exists".format(code, k, experiment))
			else:
				os.mkdir(os.path.join("workspace", experiment, k, code))

			os.symlink(
				os.path.join("..", "base", "linked_opt_trace.bc"),
				os.path.join("workspace", experiment, k, code, "linked_opt_trace.bc")
			)
			os.symlink(
				os.path.join("..", "base", "linked_opt.bc"),
				os.path.join("workspace", experiment, k, code, "linked_opt.bc")
			)
			os.symlink(
				os.path.join("..", "base", "dynamic_trace.gz"),
				os.path.join("workspace", experiment, k, code, "dynamic_trace.gz")
			)
			os.symlink(
				os.path.join("..", "base", "mem_trace.txt"),
				os.path.join("workspace", experiment, k, code, "mem_trace.txt")
			)
			os.symlink(
				os.path.join("..", "base", "mem_trace_short.bin"),
				os.path.join("workspace", experiment, k, code, "mem_trace_short.bin")
			)
			os.symlink(
				os.path.join("..", "..", "..", "..", "utils", "Makefile.linsched"),
				os.path.join("workspace", experiment, k, code, "Makefile")
			)

			with open(os.path.join("workspace", experiment, k, code, "config.cfg"), "w") as outFile:
				# Write basic onchip array info
				for a in jsonContent["arrays"]:
					if not ("offchip" in jsonContent and a in jsonContent["offchip"]):
						wordSize = int(jsonContent["arrays"][a]["size"])
						totalSize = int(jsonContent["arrays"][a]["words"]) * wordSize
						# Args are not resource-counted in Lina (unless --f-argres is active, which I believe it is... uhm anyway...)
						# By default arrays will be considered as "arg" and inarrays as "rovar/rwvar" depending on the readonly flag
						# You can override an array scope by using the "forcescope" flag
						scope = "arg"
						if "forcescope" in jsonContent["arrays"][a]:
							scope = jsonContent["arrays"][a]["forcescope"]
						outFile.write("array,{},{},{},onchip,{}\n".format(a, totalSize, wordSize, scope))

				# Write basic offchip array info
				if "offchip" in jsonContent:
					for a in jsonContent["arrays"]:
						if a in jsonContent["offchip"]:
							wordSize = int(jsonContent["arrays"][a]["size"])
							totalSize = int(jsonContent["arrays"][a]["words"]) * wordSize
							outFile.write("array,{},{},{},offchip\n".format(a, totalSize, wordSize))

				# Write inarrays, that are not going to be partitioned
				# XXX Assuming here they are always onchip!
				if "inarrays" in jsonContent:
					for a in jsonContent["inarrays"]:
						wordSize = int(jsonContent["inarrays"][a]["size"])
						totalSize = int(jsonContent["inarrays"][a]["words"]) * wordSize
						readonly = jsonContent["inarrays"][a]["readonly"] if "readonly" in jsonContent["inarrays"][a] else False
						outFile.write("array,{},{},{},{}\n".format(a, totalSize, wordSize, "rovar" if readonly else "rwvar"))

				# Write partitioning info
				for arr in cfgGen.getArrays():
					if "complete" == arr["type"]:
						wordSize = int(jsonContent["arrays"][arr["name"]]["size"])
						totalSize = int(jsonContent["arrays"][arr["name"]]["words"]) * wordSize
						outFile.write("partition,complete,{},{}\n".format(arr["name"], totalSize))
					elif arr["type"] in ("cyclic", "block"):
						wordSize = int(jsonContent["arrays"][arr["name"]]["size"])
						totalSize = int(jsonContent["arrays"][arr["name"]]["words"]) * wordSize
						outFile.write("partition,{},{},{},{},{}\n".format(arr["type"], arr["name"], totalSize, wordSize, arr["factor"]))

				# Write loop info
				for lop in cfgGen.getLoops():
					if lop["pipelining"]:
						outFile.write("pipeline,{},{},{}\n".format(k, lop["id"], lop["depth"]))
					if lop["unrolling"] is not None:
						loopCfg = jsonContent["loops"][lop["id"]]
						for j in range(lop["depth"] - 1):
							loopCfg = loopCfg["nest"]
						outFile.write("unrolling,{},{},{},{},{}\n".format(k, lop["id"], lop["depth"], loopCfg["line"], lop["unrolling"]))

				# Activate banking if it is the case
				if cfgGen.getBanking():
					outFile.write("global,ddrbanking,1\n")

		console.setProgress(0, 100, "Generating base LLVM IR and bytecodes...")
		with open(os.path.join("workspace", experiment, k, "base", "make.out"), "w") as outF:
			subprocess.run(
				["make", "lina-prepare"],
				cwd=os.path.join("workspace", experiment, k, "base"), env=modEnv, check=True,
				stderr=subprocess.DEVNULL if "yes" == options["SILENT"][1] else subprocess.STDOUT,
				stdout=subprocess.DEVNULL if "yes" == options["SILENT"][1] else outF
			)

		console.setProgress(0, 100, "Done generating for {}!".format(k))


def trace(console, options, experiment, kernels):
	modEnv = os.environ
	if options["PATH"][1] is not None:
		modEnv["PATH"] = "{}:{}".format(options["PATH"][1], modEnv["PATH"])
	linaBaseCmd = ["lina"] + ([] if "yes" == options["SILENT"][1] else ["-v"])

	for k in kernels:
		if not os.path.exists(os.path.join("workspace", experiment, k, "base")):
			raise FileNotFoundError("Base folder for kernel \"{}\" (experiment \"{}\") not found".format(k, experiment))

		# Read the json config files (following inheritance)
		jsonContent, baseExpRelPath = parseJsonFiles(experiment, k)
		loopID = 0 if "loopid" not in jsonContent else jsonContent["loopid"]

		avgTime = 0.0

		console.setProgress(0, 0, "Preparing...", "Generating trace for {}".format(k))
		with open(os.path.join("workspace", experiment, k, "base", "lina.trace.out"), "w") as outF:
			if os.path.exists(os.path.join("workspace", experiment, "base", "dynamic_trace.gz")):
				console.setProgress(0, 0, "Deleting dynamic_trace.gz...")
				os.remove(os.path.join("workspace", experiment, "base", "dynamic_trace.gz"))

			console.setProgress(0, 0, "Running \"lina\"...")

			before = time.time_ns()
			subprocess.run(
				linaBaseCmd + ["-l", "{}".format(loopID), "--mode", "trace", "--short-mem-trace", "linked_opt.bc", k],
				cwd=os.path.join("workspace", experiment, k, "base"), env=modEnv, check=True,
				stderr=subprocess.DEVNULL if "yes" == options["SILENT"][1] else subprocess.STDOUT,
				stdout=subprocess.DEVNULL if "yes" == options["SILENT"][1] else outF
			)
			after = time.time_ns()

		with open(os.path.join("workspace", experiment, k, "base", "trace.time"), "w") as timeF:
			timeF.write("{}ns\n".format(after - before))

		console.setProgress(0, 100, "Done generating trace for {}! Elapsed time: {:.3}us".format(k, (after - before) / 1000.0))


def explore(console, options, experiment, kernels):
	modEnv = os.environ
	if options["PATH"][1] is not None:
		modEnv["PATH"] = "{}:{}".format(options["PATH"][1], modEnv["PATH"])
	noOfJobs = options["JOBS"][1]

	for k in kernels:
		if not os.path.exists(os.path.join("workspace", experiment, k, "base")):
			raise FileNotFoundError("Base folder for kernel \"{}\" (experiment \"{}\") not found".format(k, experiment))

		perJobBefore = [None] * noOfJobs
		perJobAfter = [None] * noOfJobs
		perJobTimes = [0] * noOfJobs
		perJobCodes = [None] * noOfJobs

		# Read the json config files (following inheritance)
		jsonContent, baseExpRelPath = parseJsonFiles(experiment, k)
		loopID = 0 if "loopid" not in jsonContent else jsonContent["loopid"]

		if os.path.exists(os.path.join("workspace", experiment, k, "base", "explore.time")):
			os.remove(os.path.join("workspace", experiment, k, "base", "explore.time"))

		try:
			if "yes" == options["SILENT"][1]:
				outFs = [subprocess.DEVNULL] * range(noOfJobs)
			else:
				outFs = [open(os.path.join("workspace", experiment, k, "base", "lina.explore.{}.out".format(i + 1)), "w") for i in range(noOfJobs)]

			if "yes" == options["CACHE"][1]:
				for i in range(noOfJobs):
					console.setProgress(i, 0, "Removing futurecache.db.{}...".format(i), "Exploring {} (job {})".format(k, i + 1))

					if os.path.exists(os.path.join("workspace", experiment, k, "base", "futurecache.db.{}".format(i))):
						os.remove(os.path.join("workspace", experiment, k, "base", "futurecache.db.{}".format(i)))
			else:
				for i in range(noOfJobs):
					console.setProgress(i, 0, "Preparing...", "Exploring {} (job {})".format(k, i + 1))

			linaBaseCmd = [
				"make", "VERBOSE={}".format("no" if "yes" == options["SILENT"][1] else "yes"),
				"CACHE={}".format(options["CACHE"][1]),
				"LOOPID={}".format(loopID), "PLATFORM={}".format(jsonContent["platform"].upper()), "UNC={}".format(options["UNCERTAINTY"][1]),
				"VECTORISE={}".format("yes" if ("vectorise" in jsonContent and jsonContent["vectorise"]) else "no"),
				"DDRPOLICY={}".format("1" if ("ddrpolicy" in jsonContent and 1 == jsonContent["ddrpolicy"]) else "0"),
				"KERNEL={}".format(k)
			]

			# And finally run the DSE
			before = time.time_ns()
			totalPoints = len(os.listdir(os.path.join("workspace", experiment, k)))
			totalScheduled = 0
			cfgator = Configurator(jsonContent)
			# 0: Seeking for new design point
			# 1: Processing design point
			# 2: Finished
			jobsState = [0] * noOfJobs
			noMorePointsLeft = False
			threads = [None] * noOfJobs
			while jobsState.count(2) < noOfJobs:
				for j in range(noOfJobs):
					if 0 == jobsState[j]:
						if not noMorePointsLeft and cfgator.generateDesignPoint():
							if cfgator.bypass():
								continue

							code = cfgator.getCode()
							perJobCodes[j] = code
							totalScheduled += 1
							freqStr = str(int(cfgator.getFrequency()))

							console.setProgress(j, int(100 * (totalScheduled / totalPoints)), "Deleting residual files...")
							if os.path.exists(os.path.join("workspace", experiment, k, code, "{}_summary.log".format(k))):
								os.remove(os.path.join("workspace", experiment, k, code, "{}_summary.log".format(k)))

							if "yes" == options["CACHE"][1]:
								console.setProgress(j, int(100 * (totalScheduled / totalPoints)), "Creating cache soft-link...")
								if os.path.lexists(os.path.join("workspace", experiment, k, code, "futurecache.db")):
									os.remove(os.path.join("workspace", experiment, k, code, "futurecache.db"))
								os.symlink(
									os.path.join("..", "base", "futurecache.db.{}".format(j)),
									os.path.join("workspace", experiment, k, code, "futurecache.db")
								)

							threads[j] = threading.Thread(target=lambda: subprocess.run(
								linaBaseCmd + ["FREQ={}".format(cfgator.getFrequency()), "estimate"],
								cwd=os.path.join("workspace", experiment, k, code), env=modEnv, check=True,
								stderr=subprocess.DEVNULL if "yes" == options["SILENT"][1] else subprocess.STDOUT,
								stdout=outFs[j]
							))

							console.setProgress(j, int(100 * (totalScheduled / totalPoints)), code)
							perJobBefore[j] = time.time_ns()

							threads[j].start()
							jobsState[j] = 1
						else:
							console.setProgress(j, 100, "Done exploring {}! Elapsed time for this job: {:.3}us".format(k, perJobTimes[j] / 1000.0))
							with open(os.path.join("workspace", experiment, k, "base", "explore.time"), "a") as timeF:
								timeF.write("Job {}: {}ns\n".format(j + 1, perJobTimes[j]))
							noMorePointsLeft = True
							jobsState[j] = 2
					if 1 == jobsState[j]:
						if threads[j] is not None and not threads[j].is_alive():
							# Don't know if this line is needed though (haunted by zombies)
							threads[j].join()

							perJobAfter[j] = time.time_ns()
							perJobTimes[j] += perJobAfter[j] - perJobBefore[j]
							with open(os.path.join("workspace", experiment, k, perJobCodes[j], "id.file"), "w") as idF:
								idF.write("{}\n{}\n".format(j, perJobAfter[j] - perJobBefore[j]))

							#outFs[j].close()

							threads[j] = None
							jobsState[j] = 0
			after = time.time_ns()

			console.setProgress(0, 100, "Elapsed time for this job: {:.3}us; Total: {:.3}us".format(perJobTimes[0] / 1000.0, (after - before) / 1000.0))
			with open(os.path.join("workspace", experiment, k, "base", "explore.time"), "a") as timeF:
				timeF.write("All jobs: {}ns\n".format(after - before))
		finally:
			for outF in outFs:
				if outF is not subprocess.DEVNULL and not outF.closed:
					outF.close()


def collect(console, options, experiment, kernels):
	startString = "DDDG type: non-perfect loop nest (more than 1 DDDG)\n"
	pipelineStartString = "Loop pipelining enabled? yes\n"
	parsePipelineRegex = re.compile(r"Initiation interval \(if applicable\): (\d+)")
	separatorString = "=======================================================================\n"
	parseRegex = re.compile(r"Total cycles: (\d+)")
	parseResRegexes = {
		"DSP": re.compile(r"DSPs: (\d+)"),
		"FF": re.compile(r"FFs: (\d+)"),
		"LUT": re.compile(r"LUTs: (\d+)"),
		"BRAM": re.compile(r"BRAM18k: (\d+)")
	}
	parseState = 0

	for k in kernels:
		if not os.path.exists(os.path.join("workspace", experiment, k, "base")):
			raise FileNotFoundError("Base folder for kernel \"{}\" (experiment \"{}\") not found".format(k, experiment))

		# Read the json config files (following inheritance)
		jsonContent, baseExpRelPath = parseJsonFiles(experiment, k)

		console.setProgress(0, 0, "Preparing...", "Collecting {}".format(k))

		db = {}
		totalPoints = len(os.listdir(os.path.join("workspace", experiment, k)))
		totalScheduled = 0
		cfgator = Configurator(jsonContent)
		while cfgator.generateDesignPoint():
			if cfgator.bypass():
				continue

			code = cfgator.getCode()
			totalScheduled += 1
			db[code] = {}
			db[code]["period"] = cfgator.getPeriod()
			db[code]["resources"] = {}
			db[code]["pipeline-info"] = {"ii": None}
			db[code]["exec-info"] = {"job-id": None, "time-ns": None}

			console.setProgress(0, int(100 * (totalScheduled / totalPoints)), code)
			with open(os.path.join("workspace", experiment, k, code, "{}_summary.log".format(k)), "r") as rpt:
				lines = rpt.readlines()

				for l in lines:
					# Search for the cycle report table
					if 0 == parseState:
						if startString == l:
							parseState = 1
					# Searching all info
					elif 1 == parseState:
						# Separator found, work is done
						if separatorString == l:
							parseState = 0
							break

						# First searching for cycle count
						lineDecoded = parseRegex.match(l)
						if lineDecoded is not None:
							db[code]["latency"] = int(lineDecoded.group(1))
							continue

						# Then searching for resources
						for key in parseResRegexes:
							lineDecoded = parseResRegexes[key].match(l)
							if lineDecoded is not None:
								db[code]["resources"][key] = int(lineDecoded.group(1))
								continue

				if parseState != 0:
					raise RuntimeError("Parse FSM failed for kernel \"{}\" (experiment \"{}\", code \"{}\")".format(k, experiment, code))

				for l in lines:
					# Search for the pipeline information line
					if 0 == parseState:
						if pipelineStartString == l:
							if db[code]["pipeline-info"]["ii"] is not None:
								raise RuntimeError("More than one active pipeline loop found for kernel \"{}\" (experiment \"{}\", code\"{}\")".format(k, experiment, code))
							else:
								parseState = 1
					# Get pipeline II
					elif 1 == parseState:
						parsePipelineMatch = parsePipelineRegex.match(l)

						if parsePipelineMatch is not None:
							db[code]["pipeline-info"]["ii"] = int(parsePipelineMatch.group(1))
							parseState = 0

				if parseState != 0:
					raise RuntimeError("Parse FSM failed for kernel {} (experiment {}, code {})".format(k, experiment, code))

			with open(os.path.join("workspace", experiment, k, code, "id.file"), "r") as idF:
				lines = idF.readlines()
				db[code]["exec-info"]["job-id"] = int(lines[0])
				db[code]["exec-info"]["time-ns"] = int(lines[1])

		console.setProgress(0, 100, "Saving results to CSV file")

		if not os.path.exists("csvs"):
			os.mkdir("csvs")
		if not os.path.exists(os.path.join("csvs", experiment)):
			os.mkdir(os.path.join("csvs", experiment))
		#if os.path.exists(os.path.join("csvs", experiment, "{}.csv".format(k))):
		#	raise FileExistsError("File csvs/{}/{}.csv exists".format(experiment, k))

		with open(os.path.join("csvs", experiment, "{}.csv".format(k)), "w") as csvF:
			csvF.write("code,period-ns,latency,ii,exectime,dsp,ff,lut,bram,dse-job-id,dse-time-ns\n")
			for code in db:
				csvF.write("{},{},{},{},{},{},{},{},{},{},{}\n".format(
					code,
					db[code]["period"], db[code]["latency"],
					db[code]["pipeline-info"]["ii"] if db[code]["pipeline-info"]["ii"] is not None else "---", db[code]["period"] * db[code]["latency"],
					db[code]["resources"]["DSP"], db[code]["resources"]["FF"], db[code]["resources"]["LUT"], db[code]["resources"]["BRAM"],
					db[code]["exec-info"]["job-id"], db[code]["exec-info"]["time-ns"]
				))

		console.setProgress(0, 100, "Done collecting {}! Data saved to csvs/{}/{}.csv".format(k, experiment, k))


if "__main__" == __name__:
	options = {
		"SILENT": [str, "no"],
		"PATH": [str, None],
		"JOBS": [int, 1],
		"CACHE": [str, "yes"],
		"UNCERTAINTY": [float, 27.0]
	}
	filteredArgv = [x for x in sys.argv[1:] if "=" not in x]
	for x in sys.argv[1:]:
		if "=" in x:
			xSplit = x.split("=")

			if xSplit[0] not in options:
				raise KeyError("Option not found: {}".format(xSplit[0]))

			options[xSplit[0]][1] = options[xSplit[0]][0](xSplit[1])
			print("INFO: Option {} set to {}".format(xSplit[0], options[xSplit[0]][1]))

	if len(filteredArgv) < 2:
		sys.stderr.write("Usage: {} [OPTION=VALUE]... COMMAND EXPERIMENT [KERNELS]...\n".format(appName))
		sys.stderr.write("    [OPTION=VALUE]... change options (e.g. UNCERTAINTY=27.0):\n")
		sys.stderr.write("                          SILENT      all subprocesses spawned will output\n")
		sys.stderr.write("                                      to /dev/null instead of the *.out files\n")
		sys.stderr.write("                                      (use SILENT=yes to enable)\n")
		sys.stderr.write("                                      DEFAULT: no\n")
		sys.stderr.write("                          PATH        the path to Lina and its LLVM bins\n")
		sys.stderr.write("                                      (will use $PATH when omitted)\n")
		sys.stderr.write("                                      DEFAULT: empty\n")
		sys.stderr.write("                          JOBS        set the number of threads to spawn\n")
		sys.stderr.write("                                      lina\n")
		sys.stderr.write("                                      DEFAULT: 1\n")
		sys.stderr.write("                          CACHE       toggle cache use (use CACHE=no to disable)\n")
		sys.stderr.write("                                      DEFAULT: yes\n")
		sys.stderr.write("                          UNCERTAINTY (in %, only applicable to \"explore\")\n")
		sys.stderr.write("                                      DEFAULT: 27.0\n")
		sys.stderr.write("    COMMAND           may be\n")
		sys.stderr.write("                          generate\n")
		sys.stderr.write("                          trace\n")
		sys.stderr.write("                          explore\n")
		sys.stderr.write("                          collect\n")
		sys.stderr.write("    EXPERIMENT        the experiment to run (i.e. the folders in \"sources/*\")\n")
		sys.stderr.write("    [KERNELS]...      the kernels to run the command on\n")
		sys.stderr.write("                      (will consider all in the experiment when omitted)\n")
		exit(-1)

	command = filteredArgv[0]
	experiment = filteredArgv[1]
	kernels = os.listdir(os.path.join("sources", experiment)) if 2 == len(filteredArgv) else filteredArgv[2:]

	if "generate" == command:
		ProgressConsole(generate, ["Generate"], options, experiment, kernels)
	elif "trace" == command:
		ProgressConsole(trace, ["Trace"], options, experiment, kernels)
	elif "explore" == command:
		ProgressConsole(explore, ["Explore (job {})".format(i) for i in range(1, options["JOBS"][1] + 1)], options, experiment, kernels)
	elif "collect" == command:
		ProgressConsole(collect, ["Collect"], options, experiment, kernels)
	else:
		raise RuntimeError("Invalid command received: {}".format(command))
