/* Copyright 2007-2015 QReal Research Group
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

#include "switchConditionsMerger.h"

using namespace generatorBase::converters;

SwitchConditionsMerger::SwitchConditionsMerger(const QStringList &pathsToTemplates
		, const ConverterInterface * const systemVariablesConverter
		, const QStringList &values
		, bool generateIf)
	: TemplateParametrizedConverter(pathsToTemplates)
	, mSystemVariablesConverter(systemVariablesConverter)
	, mValues(values)
	, mGenerateIf(generateIf)
{
}

SwitchConditionsMerger::~SwitchConditionsMerger()
{
	delete mSystemVariablesConverter;
}

QString SwitchConditionsMerger::convert(const QString &expression) const
{
	const QString convertedExpression = mSystemVariablesConverter->convert(expression);
	const QString oneCaseFile = mGenerateIf ? "switch/oneCase.t" : "switch/oneCase_switch.t";
	const QString conditionsSeparatorFile = mGenerateIf ? "switch/conditionsSeparator.t"
			: "switch/conditionsSeparator_switch.t";

	const QString oneCondition = readTemplate(oneCaseFile);
	const QString conditionsSeparator = readTemplate(conditionsSeparatorFile);

	QStringList conditions;
	for (const QString &value : mValues) {
		QString condition = oneCondition;
		conditions << condition.replace("@@EXPRESSION@@", convertedExpression).replace("@@VALUE@@", value);
	}

	return conditions.join(conditionsSeparator);
}
