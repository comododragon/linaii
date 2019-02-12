#include "profile_h/BaseDatapath.h"

#include "profile_h/colors.h"

ColorWriter::ColorWriter(
	Graph &graph,
	VertexNameMap &vertexNameMap,
	NameVecTy &bbNames,
	NameVecTy &funcNames,
	std::vector<int> &opcodes,
	llvm::bbFuncNamePair2lpNameLevelPairMapTy &bbFuncNamePair2lpNameLevelPairMap
) : graph(graph), vertexNameMap(vertexNameMap), bbNames(bbNames), funcNames(funcNames), opcodes(opcodes), bbFuncNamePair2lpNameLevelPairMap(bbFuncNamePair2lpNameLevelPairMap) { }

template<class VE> void ColorWriter::operator()(std::ostream &out, const VE &v) const {
	unsigned nodeID = vertexNameMap[v];

	assert(nodeID < bbNames.size() && "Node ID out of bounds (bbNames)");
	assert(nodeID < funcNames.size() && "Node ID out of bounds (funcNames)");
	assert(nodeID < opcodes.size() && "Node ID out of bounds (opcodes)");

	std::string bbName = bbNames.at(nodeID);
	std::string funcName = funcNames.at(nodeID);
	llvm::bbFuncNamePairTy bbFuncPair = std::make_pair(bbName, funcName);

	llvm::bbFuncNamePair2lpNameLevelPairMapTy::iterator found = bbFuncNamePair2lpNameLevelPairMap.find(bbFuncPair);
	if(found != bbFuncNamePair2lpNameLevelPairMap.end()) {
		std::string colorString;
		switch((ColorEnum) std::get<1>(parseLoopName(found->second.first))) {
			case RED: colorString = "red"; break;
			case GREEN: colorString = "green"; break;
			case BLUE: colorString = "blue"; break;
			case CYAN: colorString = "cyan"; break;
			case GOLD: colorString = "gold"; break;
			case HOTPINK: colorString = "hotpink"; break;
			case NAVY: colorString = "navy"; break;
			case ORANGE: colorString = "orange"; break;
			case OLIVEDRAB: colorString = "olivedrab"; break;
			case MAGENTA: colorString = "magenta"; break;
			default: colorString = "black"; break;
		}

		int op = opcodes.at(nodeID);
		if(isBranchOp(op)) {
			out << "[style=filled " << colorString << " label=\"{" << nodeID << " | br}\"]";
		}
		else if(isLoadOp(op)) {
			out << "[shape=polygon sides=5 peripheries=2 " << colorString << " label=\"{" << nodeID << " | ld}\"]";
		}
		else if(isStoreOp(op)) {
			out << "[shape=polygon sides=4 peripheries=2 " << colorString << " label=\"{" << nodeID << " | st}\"]";
		}
		else if(isAddOp(op)) {
			out << "[" << colorString << " label=\"{" << nodeID << " | add}\"]";
		}
		else if(isMulOp(op)) {
			out << "[" << colorString << " label=\"{" << nodeID << " | mul}\"]";
		}
		else if(isIndexOp(op)) {
			out << "[" << colorString << " label=\"{" << nodeID << " | index}\"]";
		}
		else if(isFloatOp(op)) {
			if(isFAddOp(op)) {
				out << "[shape=diamond " << colorString << " label=\"{" << nodeID << " | fadd}\"]";
			}
			if(isFSubOp(op)) {
				out << "[shape=diamond " << colorString << " label=\"{" << nodeID << " | fsub}\"]";
			}
			if(isFMulOp(op)) {
				out << "[shape=diamond " << colorString << " label=\"{" << nodeID << " | fmul}\"]";
			}
			if(isFDivOp(op)) {
				out << "[shape=diamond " << colorString << " label=\"{" << nodeID << " | fdiv}\"]";
			}
			if(isFCmpOp(op)) {
				out << "[shape=diamond " << colorString << " label=\"{" << nodeID << " | fcmp}\"]";
			}
		}
		else if(isPhiOp(op)) {
			out << "[shape=polygon sides=4 style=filled color=gold label=\"{" << nodeID << " | phi}\"]";
		}
		else if(isBitOp(op)) {
			out << "[" << colorString << " label=\"{" << nodeID << " | bit}\"]";
		}
		else if(isCallOp(op)) {
			out << "[" << colorString << " label=\"{" << nodeID << " | call}\"]";
		}
		else {
			out << "[" << colorString << "]";
		}
	}
}

EdgeColorWriter::EdgeColorWriter(Graph &graph, EdgeWeightMap &edgeWeightMap) : graph(graph), edgeWeightMap(edgeWeightMap) { }

template<class VE> void EdgeColorWriter::operator()(std::ostream &out, const VE &e) const {
	unsigned weight = edgeWeightMap[e];

	if(CONTROL_EDGE == weight)
		out << "[color=red label=" << weight << "]";
	else
		out << "[color=black label=" << weight << "]";
}
