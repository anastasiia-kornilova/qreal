#include "outputWidget.h"

#include <QtWidgets/QVBoxLayout>

using namespace qReal::gui;

OutputWidget::OutputWidget(QWidget *parent)
	: QWidget(parent)
{
	setLayout(new QVBoxLayout);
	layout()->setContentsMargins(0, 0, 0, 0);
}

void OutputWidget::setWidget(QWidget *widget)
{
	layout()->addWidget(widget);
}
