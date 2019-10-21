#ifndef BOOSTINCLS_H
#define BOOSTINCLS_H

// If using GCC, these pragmas will stop GCC from outputting the thousands of warnings generated by boost library (WHICH IS EXTREMELY ANNOYING)
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#pragma GCC diagnostic ignored "-Wdeprecated-copy"
#pragma GCC diagnostic ignored "-Wparentheses"
#endif
#include <boost/config.hpp>
#include <boost/tokenizer.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/graph/iteration_macros.hpp>
#include <boost/graph/properties.hpp>
#include <boost/graph/topological_sort.hpp>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

using namespace llvm;

typedef boost::property<boost::vertex_index_t, unsigned> VertexProperty;
typedef boost::property<boost::edge_weight_t, uint8_t> EdgeProperty;
typedef boost::adjacency_list<boost::listS, boost::vecS, boost::bidirectionalS, VertexProperty, EdgeProperty> Graph;
typedef boost::graph_traits<Graph>::vertex_descriptor Vertex;
typedef boost::graph_traits<Graph>::edge_descriptor Edge;
typedef boost::graph_traits<Graph>::vertex_iterator VertexIterator;
typedef boost::graph_traits<Graph>::edge_iterator EdgeIterator;
typedef boost::graph_traits<Graph>::in_edge_iterator InEdgeIterator;
typedef boost::graph_traits<Graph>::out_edge_iterator OutEdgeIterator;
typedef boost::property_map<Graph, boost::edge_weight_t>::type EdgeWeightMap;
typedef boost::property_map<Graph, boost::vertex_index_t>::type VertexNameMap;

#endif
