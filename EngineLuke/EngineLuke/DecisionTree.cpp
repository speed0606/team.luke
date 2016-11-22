#include "DecisionTree.h"
#include "Dataframe.h"
#include <algorithm>
#include <iostream>
#include <set>
#include <unordered_map>
#include "Gain.h"
#include "InstanceCategorizer.h"
#include "MultiIntegralDiscretizer.h"
#include "Dataframe.h"

static std::vector<std::vector<int>> ToChildrenEntropyVec(
	const std::vector<InstanceCategorizer>& childCategorizers)
{
	std::vector<std::vector<int>> entropyVecs;
	for (const auto& categorizer : childCategorizers)
		entropyVecs.emplace_back(std::move(categorizer.GetEntropyVector()));
	return std::move(entropyVecs);
}

DecisionTree::Node::Node()
	:_discretizer(nullptr)
{
}

DecisionTree::Node::~Node()
{
}

std::string DecisionTree::Node::GetAnswer(Instance* instance)
{
	if (_children.empty())
		return _conceptClass;

	std::string childName = _discretizer->Discretize(instance);
	return _children[childName]->GetAnswer(instance);
}

void DecisionTree::Node::Walk(IDTVisitor* visitor, bool visit)
{
	if (visit && !visitor->Visit(this))
		return;

	for (auto& child : _children)
		child.second->Walk(visitor, true);
}

DecisionTree::DecisionTree(Dataframe& dataframe, int answerIdx)
	: _dataframe(dataframe)
	, _answerIdx(answerIdx)
	, _o(nullptr)
	, _root(nullptr)
{
}

void DecisionTree::SetDebugOutput(std::ofstream* o)
{
	_o = o;
}

DecisionTree::Node* DecisionTree::BuildTree(
	const std::vector<Instance*>& instances, 
	std::vector<bool>& attNoded)
{
	// classfy instances by answer classes for parent node entropy vector 
	// calculation
	InstanceCategorizer parentCategorizer(_answerIdx);
	for (auto& instance : instances)
		parentCategorizer.Add(instance);

	// if one or less classes exist, purity is 1 -> leaf node
	if (parentCategorizer.GetClassCount() <= 1)
	{
		Node* node = new Node;
		node->_conceptClass = instances[0]->GetAttribute(_answerIdx).AsString();
		return node;
	}

	// extract parent node's entropy vector
	std::vector<int> parentEntropyVec =
		std::move(parentCategorizer.GetEntropyVector());

	// calculate gain per each attribute and find the attribute with the highest
	// gain
	double bestGain = 0;
	size_t bestGainAttIdx = 0;
	InstanceCategorizer bestGainAttCategorizer(0);
	std::vector<InstanceCategorizer> bestGainAttChildCategorizers;
	std::vector<std::string> bestGainClasses;
	for (size_t attIdx = 0; attIdx < _dataframe.GetAttributeCount(); ++attIdx)
	{
		if (attNoded[attIdx])
			continue;

		// categorize instances by current attribute
		MultiIntegralDiscretizer* discretizer = 
			new MultiIntegralDiscretizer(attIdx, _answerIdx);
		discretizer->Build(instances);

		InstanceCategorizer categorizerByAtt(attIdx, discretizer);
		for (auto& instance : instances)
			categorizerByAtt.Add(instance);

		// categorize instances of each attribute clsas by concept
		// and save their states so that children entropy vectors can be
		// calculated
		std::vector<std::string> classes =
			std::move(categorizerByAtt.GetClasses());
		std::vector<InstanceCategorizer> childAnswerCategorizers;
		for (auto& classname : classes)
		{
			std::vector<Instance*> currentClassInstances =
				std::move(categorizerByAtt.GetInstances(classname));

			childAnswerCategorizers.emplace_back(_answerIdx);
			InstanceCategorizer& categorizerByAnswer = 
				childAnswerCategorizers.back();

			for (auto& instance : currentClassInstances)
				categorizerByAnswer.Add(instance);
		}

		// with classified instances for each class of current attribute,
		// generates children entropy vectors.
		std::vector<std::vector<int>> childrenEntropyVecs = 
			ToChildrenEntropyVec(childAnswerCategorizers);

		// calculate gain with calculated parent/children entropy vectors.s
		double gain = Gain(
			nullptr, _dataframe.GetAttributeName(attIdx), parentEntropyVec, 
			classes, childrenEntropyVecs, true, ""
		);

		// if current gain is better than previous attribute, update.
		if (gain > bestGain)
		{
			bestGain = gain;
			bestGainAttIdx = attIdx;
			bestGainAttCategorizer = std::move(categorizerByAtt);
			bestGainAttChildCategorizers = std::move(childAnswerCategorizers);
			bestGainClasses = std::move(classes);
		}
	}

	// create a node.
	Node* node = new Node;
	// assign attribute it will use to classify instances
	node->_attributeName = _dataframe.GetAttributeName(bestGainAttIdx);
	node->_discretizer.reset(bestGainAttCategorizer.ReleaseDiscretizer());
	attNoded[bestGainAttIdx] = true;

	// build child nodes recursively and add them to node.
	for (size_t i = 0; i < bestGainAttChildCategorizers.size(); ++i)
	{
		auto& childCategorizer = bestGainAttChildCategorizers[i];
		auto& classname = bestGainClasses[i];
		std::vector<Instance*> childInstances =
			std::move(childCategorizer.GetInstances());

		if (Node* childNode = BuildTree(childInstances, attNoded))
		{
			node->_children.emplace(classname, childNode);
		}
	}

	attNoded[bestGainAttIdx] = false;

	// return node.
	return node;
}

void DecisionTree::Build()
{
	std::vector<bool> attMarker(_dataframe.GetAttributeCount(), false);
	attMarker[_answerIdx] = true;
	_root = BuildTree(_dataframe.GetInstances(), attMarker);
}

void DecisionTree::Walk(IDTVisitor* visitor, bool visit)
{
	if (visit && !visitor->Visit(this))
		return;

	if (_root)
		_root->Walk(visitor, true);
}
