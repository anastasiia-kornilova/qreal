/* Copyright 2014-2016 QReal Research Group, CyberTech Labs Ltd.
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

#include "usbRealRobotModel.h"

#include <qrutils/singleton.h>
#include <ev3Kit/communication/usbRobotCommunicationThread.h>

using namespace ev3::robotModel::real;

UsbRealRobotModel::UsbRealRobotModel(const QString &kitId, const QString &robotId)
	: RealRobotModel(kitId, robotId, &utils::Singleton<communication::UsbRobotCommunicationThread>::instance())
{
}

QString UsbRealRobotModel::name() const
{
	return "Ev3UsbRealRobotModel";
}

QString UsbRealRobotModel::friendlyName() const
{
	return tr("Interpretation (USB)");
}

int UsbRealRobotModel::priority() const
{
	return 7;  // Right after generation mode
}
