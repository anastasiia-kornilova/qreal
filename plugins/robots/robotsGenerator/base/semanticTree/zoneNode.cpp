#include "zoneNode.h"

using namespace qReal::robots::generators::semantics;

ZoneNode::ZoneNode(QObject *parent)
	: SemanticNode(Id(), parent)
{
}

bool ZoneNode::isEmpty() const
{
	return mChildren.isEmpty();
}

void ZoneNode::appendChild(SemanticNode *node)
{
	mChildren << node;
	node->setParentNode(this);
}

void ZoneNode::appendChildren(QLinkedList<SemanticNode *> const &nodes)
{
	foreach (SemanticNode * const node, nodes) {
		appendChild(node);
	}
}

void ZoneNode::removeChild(SemanticNode *node)
{
	mChildren.removeOne(node);
	node->setParentNode(NULL);
}

SemanticNode *ZoneNode::removeLast()
{
	SemanticNode *result = mChildren.last();
	mChildren.removeLast();
	result->setParentNode(NULL);
	return result;
}

QLinkedList<SemanticNode *> ZoneNode::removeStartingFrom(SemanticNode *node)
{
	QLinkedList<SemanticNode *> result;
	bool foundNode = node == NULL;

	foreach (SemanticNode * const current, mChildren) {
		if (!foundNode) {
			foundNode = current == node;
		}

		if (foundNode) {
			current->setParentNode(NULL);
			mChildren.removeOne(current);
			result << current;
		}
	}

	return result;
}

QString ZoneNode::toString(GeneratorCustomizer &customizer, int indent) const
{
	QString result;
	foreach (SemanticNode const * const child, mChildren) {
		result += child->toString(customizer, indent).trimmed() + "\n";
	}

	return result;
}

SemanticNode *ZoneNode::parentNode()
{
	return mParentNode;
}

QLinkedList<SemanticNode *> ZoneNode::children() const
{
	return mChildren;
}
