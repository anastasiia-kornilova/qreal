#include "d2RobotModel.h"
#include "../tracer.h"
#include "../../../qrkernel/settingsManager.h"
#include "../../../../../qrutils/mathUtils/gaussNoise.h"

using namespace qReal::interpreters::robots;
using namespace details;
using namespace d2Model;
using namespace mathUtils;



unsigned long const black   = 0xFF000000;
unsigned long const white   = 0xFFFFFFFF;
unsigned long const red     = 0xFFFF0000;
unsigned long const green   = 0xFF008000;
unsigned long const blue    = 0xFF0000FF;
unsigned long const yellow  = 0xFFFFFF00;
unsigned long const cyan    = 0xFF00FFFF;
unsigned long const magenta = 0xFFFF00FF;


unsigned const touchSensorPressedSignal = 1;
unsigned const touchSensorNotPressedSignal = 0;

qreal const spoilColorDispersion = 2.0;
qreal const spoilLightDispersion = 1.0;
qreal const spoilSonarDispersion = 1.5;
qreal const varySpeedDispersion = 0.0125;
qreal const percentSaltPepperNoise = 20.0;

D2RobotModel::D2RobotModel(QObject *parent)
	: QObject(parent)
	, mD2ModelWidget(NULL)
	, mMotorA(NULL)
	, mMotorB(NULL)
	, mMotorC(NULL)
	, mDisplay(new NxtDisplay)
	, mTimeline(new Timeline(this))
	, mNoiseGen()
	, mNeedSync(false)
	, mNeedSensorNoise(SettingsManager::value("enableNoiseOfSensors").toBool())
	, mNeedMotorNoise(SettingsManager::value("enableNoiseOfMotors").toBool())
{
	mAngle = 0;
	VK_mMomentI = 100;
	mPos = QPointF(0,0);
	mFric = 0;
	mNoiseGen.setApproximationLevel(SettingsManager::value("approximationLevel").toUInt());
	connect(mTimeline, SIGNAL(tick()), this, SLOT(recalculateParams()), Qt::UniqueConnection);
	connect(mTimeline, SIGNAL(nextFrame()), this, SLOT(nextFragment()), Qt::UniqueConnection);
	initPosition();
}

D2RobotModel::~D2RobotModel()
{
}

void D2RobotModel::initPosition()
{
	if (mMotorA) {
		delete mMotorA;
	}
	if (mMotorB) {
		delete mMotorB;
	}
	if (mMotorC) {
		delete mMotorC;
	}
	mMotorA = initMotor(robotWheelDiameterInPx / 2, 0, 0, 0, false);
	mMotorB = initMotor(robotWheelDiameterInPx / 2, 0, 0, 1, false);
	mMotorC = initMotor(robotWheelDiameterInPx / 2, 0, 0, 2, false);
	setBeep(0, 0);
	mPos = mD2ModelWidget ? mD2ModelWidget->robotPos() : QPointF(0, 0);
}

void D2RobotModel::clear()
{
	initPosition();
	mAngle = 0;
	mPos = QPointF(0,0);
}

D2RobotModel::Motor* D2RobotModel::initMotor(int radius, int speed, long unsigned int degrees, int port, bool isUsed)
{
	Motor *motor = new Motor();
	motor->VK_mMotorFactor = 0;
	motor->radius = radius;
	motor->speed = speed;
	motor->degrees = degrees;
	motor->isUsed = isUsed;
	if (degrees == 0) {
		motor->activeTimeType = DoInf;
	} else {
		motor->activeTimeType = DoByLimit;
	}
	mMotors[port] = motor;
	mTurnoverMotors[port] = 0;
	return motor;
}

void D2RobotModel::setBeep(unsigned freq, unsigned time)
{
	mBeep.freq = freq;
	mBeep.time = time;
}

void D2RobotModel::setNewMotor(int speed, uint degrees, const int port)
{
	mMotors[port]->speed = speed;
	mMotors[port]->degrees = degrees;
	mMotors[port]->isUsed = true;
	if (degrees == 0) {
		mMotors[port]->activeTimeType = DoInf;
	} else {
		mMotors[port]->activeTimeType = DoByLimit;
	}
	if (speed != 0) mMotors[port]->VK_mMotorFactor = 0.5;
}

int D2RobotModel::varySpeed(int const speed) const
{
	qreal const ran = mNoiseGen.generate(mNoiseGen.approximationLevel(), varySpeedDispersion);
	return truncateToInterval(-100, 100, round(speed * (1 + ran)));
}

void D2RobotModel::countMotorTurnover()
{
	foreach (Motor * const motor, mMotors) {
		int const port = mMotors.key(motor);
		qreal const degrees = Timeline::timeInterval * motor->speed * onePercentAngularVelocity;
		mTurnoverMotors[port] += degrees;
		if (motor->isUsed && (motor->activeTimeType == DoByLimit) && (mTurnoverMotors[port] >= motor->degrees)) {
			motor->speed = 0;
			motor->activeTimeType = End;
			emit d2MotorTimeout();
		}
	}
}

int D2RobotModel::readEncoder(int/*inputPort::InputPortEnum*/ const port) const
{
	return mTurnoverMotors[port];
}

void D2RobotModel::resetEncoder(int/*inputPort::InputPortEnum*/ const port)
{
	mTurnoverMotors[port] = 0;
}

SensorsConfiguration &D2RobotModel::configuration()
{
	return mSensorsConfiguration;
}

D2ModelWidget *D2RobotModel::createModelWidget()
{
	mD2ModelWidget = new D2ModelWidget(this, &mWorldModel, mDisplay);
	return mD2ModelWidget;
}

QPair<QPointF, qreal> D2RobotModel::countPositionAndDirection(robots::enums::inputPort::InputPortEnum const port) const
{
	QVector<SensorItem *> items = mD2ModelWidget->sensorItems();
	SensorItem *sensor = items[port];
	QPointF const position = sensor ? sensor->scenePos() : QPointF();
	qreal const direction = sensor ? items[port]->rotation() + mAngle : 0;
	return QPair<QPointF, qreal>(position, direction);
}

int D2RobotModel::readTouchSensor(robots::enums::inputPort::InputPortEnum const port)
{
	if (mSensorsConfiguration.type(port) != robots::enums::sensorType::touchBoolean
			&& mSensorsConfiguration.type(port) != robots::enums::sensorType::touchRaw)
	{
		return touchSensorNotPressedSignal;
	}
	QPair<QPointF, qreal> neededPosDir = countPositionAndDirection(port);
	QPointF sensorPosition(neededPosDir.first);
	qreal const width = sensorWidth / 2.0;
	QRectF const scanningRect = QRectF(
				sensorPosition.x() - width - touchSensorStrokeIncrement / 2.0
				, sensorPosition.y() - width - touchSensorStrokeIncrement / 2.0
				, 2 * width + touchSensorStrokeIncrement
				, 2 * width + touchSensorStrokeIncrement);

	QPainterPath sensorPath;
	sensorPath.addRect(scanningRect);
	bool const res = mWorldModel.checkCollision(sensorPath, touchSensorWallStrokeIncrement);

	return res ? touchSensorPressedSignal : touchSensorNotPressedSignal;
}

int D2RobotModel::readSonarSensor(robots::enums::inputPort::InputPortEnum const port) const
{
	QPair<QPointF, qreal> neededPosDir = countPositionAndDirection(port);
	int const res = mWorldModel.sonarReading(neededPosDir.first, neededPosDir.second);

	return mNeedSensorNoise ? spoilSonarReading(res) : res;
}

int D2RobotModel::spoilSonarReading(int const distance) const
{
	qreal const ran = mNoiseGen.generate(
				mNoiseGen.approximationLevel()
				, spoilSonarDispersion
				);

	return truncateToInterval(0, 255, round(distance + ran));
}

int D2RobotModel::readColorSensor(robots::enums::inputPort::InputPortEnum const port) const
{
	QImage const image = printColorSensor(port);
	QHash<uint, int> countsColor;

	uint const *data = reinterpret_cast<uint const *>(image.bits());
	int const n = image.byteCount() / 4;
	for (int i = 0; i < n; ++i) {
		uint const color = mNeedSensorNoise ? spoilColor(data[i]) : data[i];
		++countsColor[color];
	}

	switch (mSensorsConfiguration.type(port)) {
	case robots::enums::sensorType::colorFull:
		return readColorFullSensor(countsColor);
	case robots::enums::sensorType::colorNone:
		return readColorNoneSensor(countsColor, n);
	case robots::enums::sensorType::colorRed:
		return readSingleColorSensor(red, countsColor, n);
	case robots::enums::sensorType::colorGreen:
		return readSingleColorSensor(green, countsColor, n);
	case robots::enums::sensorType::colorBlue:
		return readSingleColorSensor(blue, countsColor, n);
	default:
		return 0;
	}
}

uint D2RobotModel::spoilColor(uint const color) const
{
	qreal const ran = mNoiseGen.generate(
				mNoiseGen.approximationLevel()
				, spoilColorDispersion
				);

	int r = round(((color >> 16) & 0xFF) + ran);
	int g = round(((color >> 8) & 0xFF) + ran);
	int b = round(((color >> 0) & 0xFF) + ran);
	int const a = (color >> 24) & 0xFF;

	r = truncateToInterval(0, 255, r);
	g = truncateToInterval(0, 255, g);
	b = truncateToInterval(0, 255, b);

	return ((r & 0xFF) << 16) + ((g & 0xFF) << 8) + (b & 0xFF) + ((a & 0xFF) << 24);
}

QImage D2RobotModel::printColorSensor(robots::enums::inputPort::InputPortEnum const port) const
{
	if (mSensorsConfiguration.type(port) == robots::enums::sensorType::unused) {
		return QImage();
	}
	QPair<QPointF, qreal> const neededPosDir = countPositionAndDirection(port);
	QPointF const position = neededPosDir.first;
	qreal const width = sensorWidth / 2.0;
	QRectF const scanningRect = QRectF(position.x() -  width, position.y() - width
									   , 2 * width, 2 * width);

	QImage image(scanningRect.size().toSize(), QImage::Format_RGB32);
	QPainter painter(&image);

	QBrush brush(Qt::SolidPattern);
	brush.setColor(Qt::white);
	painter.setBrush(brush);
	painter.setPen(QPen(Qt::black));
	painter.drawRect(mD2ModelWidget->scene()->itemsBoundingRect().adjusted(-width, -width, width, width));

	bool const wasSelected = mD2ModelWidget->sensorItems()[port]->isSelected();
	mD2ModelWidget->setSensorVisible(port, false);
	mD2ModelWidget->scene()->render(&painter, QRectF(), scanningRect);
	mD2ModelWidget->setSensorVisible(port, true);
	mD2ModelWidget->sensorItems()[port]->setSelected(wasSelected);

	return image;
}

int D2RobotModel::readColorFullSensor(QHash<uint, int> const &countsColor) const
{
	if (countsColor.isEmpty()) {
		return 0;
	}

	QList<int> const values = countsColor.values();
	int maxValue = INT_MIN;
	foreach (int value, values) {
		if (value > maxValue) {
			maxValue = value;
		}
	}

	uint const maxColor = countsColor.key(maxValue);
	switch (maxColor) {
	case (black):
		Tracer::debug(tracer::enums::d2Model, "D2RobotModel::readColorFullSensor", "BLACK");
		return 1;
	case (red):
		Tracer::debug(tracer::enums::d2Model, "D2RobotModel::readColorFullSensor", "RED");
		return 5;
	case (green):
		Tracer::debug(tracer::enums::d2Model, "D2RobotModel::readColorFullSensor", "GREEN");
		return 3;
	case (blue) :
		Tracer::debug(tracer::enums::d2Model, "D2RobotModel::readColorFullSensor", "BLUE");
		return 2;
	case (yellow):
		Tracer::debug(tracer::enums::d2Model, "D2RobotModel::readColorFullSensor", "YELLOW");
		return 4;
	case (white):
		Tracer::debug(tracer::enums::d2Model, "D2RobotModel::readColorFullSensor", "WHITE");
		return 6;
	case (cyan):
		Tracer::debug(tracer::enums::d2Model, "D2RobotModel::readColorFullSensor", "CYAN");
		return 7;
	case (magenta):
		Tracer::debug(tracer::enums::d2Model, "D2RobotModel::readColorFullSensor", "MAGENTA");
		return 8;
	default:
		Tracer::debug(tracer::enums::d2Model, "D2RobotModel::readColorFullSensor", "Other Color");
		return 0;
	}
}

int D2RobotModel::readSingleColorSensor(uint color, QHash<uint, int> const &countsColor, int n) const
{
	return (static_cast<double>(countsColor[color]) / static_cast<double>(n)) * 100.0;
}

int D2RobotModel::readColorNoneSensor(QHash<uint, int> const &countsColor, int n) const
{
	double allWhite = static_cast<double>(countsColor[white]);

	QHashIterator<uint, int> i(countsColor);
	while(i.hasNext()) {
		i.next();
		uint const color = i.key();
		if (color != white) {
			int const b = (color >> 0) & 0xFF;
			int const g = (color >> 8) & 0xFF;
			int const r = (color >> 16) & 0xFF;
			qreal const k = qSqrt(static_cast<qreal>(b * b + g * g + r * r)) / 500.0;
			allWhite += static_cast<qreal>(i.value()) * k;
		}
	}

	return (allWhite / static_cast<qreal>(n)) * 100.0;
}

int D2RobotModel::readLightSensor(robots::enums::inputPort::InputPortEnum const port) const
{
	// Must return 1023 on white and 0 on black normalized to percents
	// http://stackoverflow.com/questions/596216/formula-to-determine-brightness-of-rgb-color

	QImage const image = printColorSensor(port);
	if (image.isNull()) {
		return 0;
	}

	uint sum = 0;
	uint const *data = reinterpret_cast<uint const *>(image.bits());
	int const n = image.byteCount() / 4;

	for (int i = 0; i < n; ++i) {
		int const color = mNeedSensorNoise ? spoilLight(data[i]) : data[i];
		int const b = (color >> 0) & 0xFF;
		int const g = (color >> 8) & 0xFF;
		int const r = (color >> 16) & 0xFF;
		// brightness in [0..256]
		int const brightness = 0.2126 * r + 0.7152 * g + 0.0722 * b;

		sum += 4 * brightness; // 4 = max sensor value / max brightness value
	}
	qreal const rawValue = sum / n; // Average by whole region
	return rawValue * 100 / maxLightSensorValur; // Normalizing to percents
}

uint D2RobotModel::spoilLight(uint const color) const
{
	qreal const ran = mNoiseGen.generate(
				mNoiseGen.approximationLevel()
				, spoilLightDispersion
				);

	if (ran > (1.0 - percentSaltPepperNoise / 100.0)) {
		return white;
	} else if (ran < (-1.0 + percentSaltPepperNoise / 100.0)) {
		return black;
	}

	return color;
}


void D2RobotModel::startInit()
{
	initPosition();
	mPos = QPointF (0, 0);
	mAngle = 0;
	mTimeline->start();
}

void D2RobotModel::startInterpretation()
{
	startInit();

	mPos = QPointF(0,0);
	VK_mMass = 1000;
	VK_mAngularVelocity = 0;
	VK_mForceMoment = 0;
	VK_updateCoord();
	mD2ModelWidget->startTimelineListening();
}

void D2RobotModel::stopRobot()
{
	mMotorA->speed = 0;
	mMotorB->speed = 0;
	mMotorC->speed = 0;
	mD2ModelWidget->stopTimelineListening();
}

void D2RobotModel::countBeep()
{
	if (mBeep.time > 0) {
		mD2ModelWidget->drawBeep(true);
		mBeep.time -= Timeline::frameLength;
	} else {
		mD2ModelWidget->drawBeep(false);
	}
}

qreal D2RobotModel::VK_vectorProduct(QVector2D vector1, QVector2D vector2)
{
	return  vector1.x()*vector2.y() - vector1.y()*vector2.x();
}

qreal D2RobotModel::VK_scalarProduct(QVector2D vector1, QVector2D vector2)
{
	return vector1.x()*vector2.x() + vector1.y()*vector2.y();
}

QVector2D D2RobotModel::VK_getVA()const
{
	return VK_mVA;
}

QVector2D D2RobotModel::VK_getVB()const

{
	return VK_mVB;
}
QVector2D D2RobotModel::VK_getV()const

{
	return VK_mV;
}
void D2RobotModel::VK_setV(QVector2D& V)
{
	qreal V0 = VK_getFullSpeed();
	qreal V0A = VK_getFullSpeedA();
	qreal V0B = VK_getFullSpeedB();
	qreal V0_ = V0 > (V0A +V0B)/2. ? V0 : (V0A +V0B)/2.;
	V0_ = V0;
	if(V.length() > V0_)
	{
		qreal x = V0_/V.length();
		V *= x;
	}
	VK_mV = V;
}

QLineF D2RobotModel::VK_nearRobotLine(WallItem& wall, QPointF p) // Возвращает наименьший из перпендикуляров от точки до ребер стены
{
	QList<qreal> len;
	qreal min=0;
	for(int i = 0; i<4; i++)
	{
		QLineF l;
		l= wall.VK_getLine(i);
		QPointF normPoint = VK_normalPoint(l.x1(), l.y1(), l.x2(), l.y2(), p.rx(),p.ry());
		QLineF k (p.rx(), p.ry(), normPoint.rx(),normPoint.ry());
		len.push_back( k.length());
		if (min == 0){
			min = k.length();
		}
		if (k.length() < min){
			min = k.length();
		}
	}
	int j = len.indexOf(min);
	return wall.VK_getLine(j);
}

QPointF D2RobotModel::VK_normalPoint(qreal x1, qreal y1, qreal x2, qreal y2, qreal x3, qreal y3) // Возвращает точку прямой, в которую опустится перпендикуляр из заданной точки

{
	if (x1 == x2) return QPointF(x2, y3);
	qreal x0 = (x1*(y2-y1)*(y2-y1) + x3*(x2-x1)*(x2-x1) + (x2-x1)*(y2-y1)*(y3-y1))/((y2-y1)*(y2-y1) + (x2-x1)*(x2-x1));
	qreal y0 = ((y2-y1)*(x0-x1)/(x2-x1))+y1;
	return QPointF(x0, y0);
}
void D2RobotModel::VK_updateCoord() // Обновляет координаты вершин

{


	double x = mPos.rx()+25;
	double y = mPos.ry()+25;
	double nx = cos(mAngle * M_PI/180);
	double ny = sin(mAngle * M_PI/180);

	int mSize = 50;// //!!!!!!!!!!!!!!!!!!!!!

	double nnx = nx*(mSize/2);
	double nny = ny*(mSize/2);

	VK_mP[0] = QPointF(x + nnx + nny, y + nny - nnx);
	VK_mP[1] = QPointF(x + nnx - nny, y + nny + nnx);
	VK_mP[2] = QPointF(x - nnx - nny, y - nny + nnx);
	VK_mP[3] = QPointF(x - nnx + nny, y - nny - nnx);

	for (int i = 0; i < 3; i++) {
		VK_mL[i] = QLineF(VK_mP[i], VK_mP[i+1]);
	}
	VK_mL[3] = QLineF(VK_mP[3], VK_mP[0]);

}

bool D2RobotModel::VK_isRobotWallCollision(WallItem &wall)
{
	QPainterPath const boundingRegion = mD2ModelWidget->robotBoundingPolygon(mPos, mAngle);
	return boundingRegion.intersects(wall.VK_mWallPath);
}
/*
bool D2RobotModel::VK_isRobotWallCollision(WallItem &wall)
{


	return ((wall.contains(VK_mP[0]))||(wall.contains(VK_mP[1]))||(wall.contains(VK_mP[2]))||(wall.contains(VK_mP[3])));
}
*/
void D2RobotModel::VK_checkCollision(WallItem& wall) // Проверяет коллизии
{
	VK_mEdP.clear();
	QPainterPath const boundingRegion = mD2ModelWidget->robotBoundingPolygon(mPos, mAngle);
	if (VK_isRobotWallCollision(wall)){

		if (wall.isCircle()) {

			if ((wall.contains(VK_mP[0]))||(wall.contains(VK_mP[1]))||(wall.contains(VK_mP[2]))||(wall.contains(VK_mP[3]))) {
				for(int i = 0; i < 4; i++)
				{
					if (wall.contains(VK_mP[i])) {
						QLineF border = VK_tangentLine(wall); // добавить функц
						QPointF normPoint = VK_normalPoint(border.x1(), border.y1(), border.x2(), border.y2(), VK_mP[i].rx(), VK_mP[i].ry());
						QVector2D n (-normPoint.rx() + VK_mP[i].rx(), -normPoint.ry() + VK_mP[i].ry());
						n = n.normalized();
						if (!(n.length() < 1.e-10)) {
							QVector2D V = VK_getV();
							qreal k = VK_scalarProduct(V, n);
							QVector2D V1 (V - n * k);
							VK_setV(V1);
						}
						VK_setWall(i, &wall);
					}

				}

			} else {
				QLineF border = VK_interWallLine(wall); // добавить функц
				QPointF normPoint = VK_normalPoint(border.x1(), border.y1(), border.x2(), border.y2(), wall.VK_mCenter.rx(), wall.VK_mCenter.ry());
				//QPointF p = wall.VK_getPoint(i); //

				QVector2D n (normPoint.rx() - wall.VK_mCenter.rx(), normPoint.ry() - wall.VK_mCenter.ry());
				n = n.normalized();

				QPointF p = QPointF(wall.VK_mCenter + n.toPointF() * wall.width());

				QVector2D V1 (0,0);
				VK_setV(V1);
				VK_mEdP.push_back(p);
				VK_mAngularVelocity=0;
				VK_setEdgeWall(0, &wall);//костыль
			}


		} else {
			for(int i = 0; i < 4; i++)
			{
				QPointF new_p = VK_mP[i];
				if (wall.VK_mWallPath.contains(new_p)) {
					if(VK_mRobotWalls[i] == NULL) {
						QLineF border = VK_interRobotLine(wall);
						QPointF normPoint = VK_normalPoint(border.x1(), border.y1(), border.x2(), border.y2(), VK_mP[i].rx(), VK_mP[i].ry());

						QVector2D n (-normPoint.rx() + VK_mP[i].rx(), -normPoint.ry() + VK_mP[i].ry());
						n = n.normalized();
						if (!(n.length() < 1.e-10)) {
							QVector2D V = VK_getV();
							qreal k = VK_scalarProduct(V, n);
							QVector2D V1 (V - n * k);
							VK_setV(V1);
						}
					}
					VK_setWall(i, &wall);
				} else {
					if (VK_mRobotWalls[i] == &wall){
						VK_setWall(i, NULL);
					}



					QPointF p = wall.VK_getPoint(i);
					if (boundingRegion.contains(p)) {
						QLineF border = VK_interWallLine(wall);
						QPointF normPoint = VK_normalPoint(border.x1(), border.y1(), border.x2(), border.y2(), p.rx(), p.ry());
						QVector2D n (-normPoint.rx() + p.rx(), -normPoint.ry() + p.ry());
						n = n.normalized();
						QVector2D V1 (0,0);
						VK_setV(V1);
						VK_mEdP.push_back(p);
						VK_mAngularVelocity=0;
					} else {
						if (VK_mRobotEdgeWalls[i] == &wall){
							VK_setEdgeWall(i, NULL);
						}
					}
				}
			}
		}
	} else {
		for (int i = 0; i < 4; i++) {

			if (VK_mRobotWalls[i] == &wall) {
				VK_mRobotWalls[i] = NULL;
				VK_mAngularVelocity = 0;
			}
		}
	}


	qreal a = 0;
	a = a +1;

}

void D2RobotModel::countNewCoord()
{
	VK_updateCoord();

	Motor *motor1 = mMotorA;
	Motor *motor2 = mMotorB;
	Motor *motor3 = mMotorC;

	if (mMotorC->isUsed) {
		if (!mMotorA->isUsed) {
			motor1 = mMotorC;
		} else if (!mMotorB->isUsed) {
			motor2 = mMotorC;
		}
	}

	int const sspeed1 = mNeedMotorNoise ? varySpeed(motor1->speed) : motor1->speed;
	int const sspeed2 = mNeedMotorNoise ? varySpeed(motor2->speed) : motor2->speed;

	VK_mFullSpeedA = sspeed1 * 2 * M_PI * motor1->radius * onePercentAngularVelocity / 360;
	VK_mFullSpeedB = sspeed2 * 2 * M_PI * motor2->radius * onePercentAngularVelocity / 360;
	VK_mFullSpeed = motor3->speed * 2 * M_PI * motor3->radius * onePercentAngularVelocity / 360;

	VK_setForce(QVector2D(0,0));
	VK_setForceMoment(0);

	qreal rotationalFricFactor = VK_getV().length()*1500;
	qreal angularVelocityFricFactor = fabs(VK_mAngularVelocity*1000);








	QVector2D napr (cos(mAngle * M_PI / 180), sin (mAngle * M_PI / 180));

	qreal VA = VK_getFullSpeedA();
	qreal VB = VK_getFullSpeedB();
	qreal V0 = VK_getFullSpeed();
	qreal scalProdA = VK_scalarProduct(VK_getVA(), napr);
	qreal scalProdB = VK_scalarProduct(VK_getVB(), napr);
	qreal scalProd = VK_scalarProduct(VK_getV(), napr);
	qreal tmpA = ( VA - scalProdA) * mMotorB->VK_mMotorFactor;
	qreal tmpB = ( VB - scalProdB) * mMotorC->VK_mMotorFactor;
	qreal tmp2 = ( V0 - scalProd) * mMotorA->VK_mMotorFactor;
	QVector2D F_engine = napr*tmp2;
	QPointF p0 (mPos.rx() + 25, mPos.ry() + 25);
	VK_mForce = F_engine;


	QVector2D tmpAr(VK_mP[0].rx() - p0.rx(), VK_mP[0].ry() - p0.ry());
	QVector2D tmpBr(VK_mP[1].rx() - p0.rx(), VK_mP[1].ry() - p0.ry());

	QVector2D F_engine_A = napr*tmpA;
	QVector2D F_engine_B = napr*tmpB;


	VK_mForce  += F_engine_A;
	VK_mForce  += F_engine_B;



	qreal mForceMomentA = VK_vectorProduct(F_engine_A, tmpAr);
	qreal mForceMomentB = VK_vectorProduct(F_engine_B, tmpBr);
	VK_mForceMoment -= mForceMomentA;
	VK_mForceMoment -= mForceMomentB;

	for (int i = 0; i < 4; i++) {
		if (VK_mRobotEdgeWalls[i] != NULL){
			for (int j = 0; j < VK_mEdP.length(); j++) {
				QVector2D tmp(VK_mEdP.at(j) - p0);
				QVector2D F_norm = VK_mForce;
				F_norm *= -1;
				qreal a = VK_vectorProduct(F_norm, tmp);
				VK_mForceMoment -= a;
			}
		}
	}

	for (int i = 0; i < 4; i++) {
		if (VK_mRobotWalls[i] != NULL){

			QVector2D tmp(VK_mP[i] - p0);

			QLineF bord = VK_nearRobotLine(*VK_mRobotWalls[i], VK_mP[i]);
			qreal ang = bord.angle();

			QVector2D vectorParalStene;
			if (ang == 90) {
				vectorParalStene = QVector2D(0, -1);
			} else {
				vectorParalStene = QVector2D(cos(ang*M_PI/180),-sin(ang*M_PI/180));
			}

			qreal scpr = VK_scalarProduct(vectorParalStene, napr);
			if (!(scpr == 0)) {
				if (scpr < 0) {
					vectorParalStene *= (-1);
				}
				vectorParalStene = vectorParalStene.normalized();

				QVector2D F_norm = VK_mForce;

				double sc1 = VK_scalarProduct(VK_mForce, vectorParalStene);
				F_norm -= vectorParalStene* sc1;

				F_norm *= -1;
				QVector2D F_fr_wall;

				vectorParalStene *= -1;

				F_fr_wall = vectorParalStene;
				F_fr_wall *= F_norm.length() * 0.2;

				VK_mForce += F_norm;
				VK_mForce += F_fr_wall;

				qreal a = VK_vectorProduct(F_fr_wall, tmp);
				qreal b = VK_vectorProduct(F_norm , tmp);

				VK_setForceMoment(VK_mForceMoment - a);
				VK_setForceMoment(VK_mForceMoment - b);

			} else {
				VK_setForceMoment(0);
				VK_mForce = QVector2D(0, 0);
			}
		}
	}

	QVector2D V = VK_getV();
	QVector2D V1 = V + VK_mForce / VK_mMass * Timeline::timeInterval;
	VK_setV(V1);// /////////////////

	qreal momentI = VK_getInertiaMoment();
	VK_mAngularVelocity += VK_mForceMoment /  momentI * Timeline::timeInterval;

	qreal fric = angularVelocityFricFactor /  momentI * Timeline::timeInterval;
	//qreal fric = VK_mAngularVelocity * 0.2;
	//if (fric < 0.05) fric = 0.05;

	qreal tmpAngVel = VK_mAngularVelocity;
	if (VK_mAngularVelocity > 0) {
		VK_mAngularVelocity -= fric;
	} else {
		VK_mAngularVelocity += fric;
	}
	if (tmpAngVel * VK_mAngularVelocity <= 0) {
		VK_mAngularVelocity = 0;
	}

	//qreal timeInterval = Timeline::timeInterval;
	//mAngle += VK_mAngularVelocity * timeInterval;


	QVector2D rotationalFrictionF (-napr.y(), napr.x());

	rotationalFrictionF = rotationalFrictionF.normalized();

	QVector2D tmpV = VK_getV();
	if (tmpV.length() != 0) {
		tmpV = tmpV.normalized();
	}
	qreal sinus = VK_vectorProduct(tmpV, rotationalFrictionF);

	rotationalFrictionF = rotationalFrictionF *(sinus * rotationalFricFactor);

	V = VK_getV();
	if (VK_scalarProduct(rotationalFrictionF, V) > 0) {
		rotationalFrictionF = -rotationalFrictionF;
	}

	QVector2D newV = V + rotationalFrictionF / VK_mMass * Timeline::timeInterval;
	qreal sc_1 = VK_scalarProduct(newV, rotationalFrictionF);
	if (sc_1 > 0) {
		qreal sc_2 = -VK_scalarProduct(V, rotationalFrictionF);
		qreal dt_tmp = Timeline::timeInterval *sc_2/(sc_2 + sc_1);
		QVector2D V1 = V + rotationalFrictionF / VK_mMass * dt_tmp;
		VK_setV(V1);

	}  else {
		VK_setV(newV);
	}
	V = VK_getV();
	if ( V.length() > V0){
		newV = V.normalized() * V0;
		VK_setV(newV);
	}



}

void D2RobotModel::VK_getRobotFromWall(WallItem& wall, int index)
{
	QPainterPath boundingRegion = mD2ModelWidget->robotBoundingPolygon(mPos, mAngle);
	if (VK_isRobotWallCollision(wall)) {
		if (wall.isCircle()) {
			for (int i = 0; i < 4; i++) {
				if (wall.VK_mWallPath.contains(VK_mP[i])) {
					QVector2D n(robotPos() - wall.VK_mCenter);
					QLineF lineN(robotPos(),  wall.VK_mCenter);
					n = n.normalized();
					QPointF pntInter = wall.VK_mCenter + n.toPointF()*wall.width();

					QPointF p(pntInter - VK_mP[i]);
					mPos = (robotPos() + p);
					return;
				}
			}
			QLineF border = VK_interWallLine(wall); // добавить функц
			QPointF normPoint = VK_normalPoint(border.x1(), border.y1(), border.x2(), border.y2(), wall.VK_mCenter.rx(), wall.VK_mCenter.ry());
			//QPointF p = wall.VK_getPoint(i); //

			QVector2D n (normPoint.rx() - wall.VK_mCenter.rx(), normPoint.ry() - wall.VK_mCenter.ry());
			n = n.normalized();

			QPointF p = QPointF(wall.VK_mCenter + n.toPointF() * wall.width());

			mPos = robotPos() + p - normPoint;
		} else {

			if (boundingRegion.intersects(wall.VK_mWallPath)){
				QPointF p = VK_mP[index];
				QLineF border = VK_nearRobotLine(wall, p);
				QPointF pntIntersect = VK_normalPoint(border.x1(), border.y1(), border.x2(), border.y2(), p.rx(), p.ry());
				QPointF k;
				k = pntIntersect -= p;
				QPointF curPos = robotPos();
				mPos = (curPos+=k);
				VK_updateCoord();
			}
		}
	}



	if (VK_isRobotWallCollision(wall))
	{
		qreal a = 0;
		a = a+1;
	}

}

void D2RobotModel::VK_getEdgeRobotFromWall(WallItem& wall, int index)
{
	QPainterPath const boundingRegion = mD2ModelWidget->robotBoundingPolygon(mPos, mAngle);

	if (boundingRegion.intersects(wall.VK_mWallPath)){
		if (wall.isCircle()) {
			QLineF l = VK_interWallLine(wall);
			QLineF l2 = VK_intersectRobotLine(wall);

			QPointF pntIntersect = VK_normalPoint(l.x1(), l.y1(), l.x2(), l.y2(), wall.VK_mCenter.rx(), wall.VK_mCenter.ry());
			QPointF pntIntersect2 = VK_normalPoint(l2.x1(), l2.y1(), l2.x2(), l2.y2(), wall.VK_mCenter.rx(), wall.VK_mCenter.ry());


			QVector2D n(pntIntersect - wall.VK_mCenter);
			QVector2D n2(pntIntersect2 - wall.VK_mCenter);

			n = n - n2;

			mPos += n.toPointF();
			return;

		} else {
			QLineF l = VK_mL[index];
			for (int i = 0; i<4; i++)
			{
				QPointF p = wall.VK_getPoint(i);
				if (boundingRegion.contains(p)){
					QPointF pntIntersect = VK_normalPoint(l.x1(), l.y1(), l.x2(), l.y2(), p.rx(), p.ry());
					QPointF k (p.rx() - pntIntersect.rx(),p.ry() - pntIntersect.ry());
					QPointF curPos = robotPos();
					mPos = (curPos+=k);
					VK_updateCoord();
				}
			}
		}
	}
}

QLineF D2RobotModel::VK_tangentLine(WallItem& wall)
{
	QVector2D n(robotPos() - wall.VK_mCenter);
	QLineF lineN(robotPos() , wall.VK_mCenter);
	n = n.normalized();
	QPointF pntInter = wall.VK_mCenter + n.toPointF()*wall.width();
	lineN = lineN.normalVector();
	QPointF p2(lineN.p1() - lineN.p2());
	return QLineF(pntInter, pntInter + p2);
}


QLineF D2RobotModel::VK_interRobotLine(WallItem& wall)
{
	QPainterPath const boundingRegion = mD2ModelWidget->robotBoundingPolygon(mPos, mAngle);






	/*
		for(int i = 0; i < 4; i++)
		{
			if (wall.VK_mWallPath.contains(VK_mP[i])) {
				QLineF line1;
				QLineF line2;
				if (i == 0) {
					line1 = VK_mL[0];
					line2 =	VK_mL[3];
				} else if (i == 1) {
					line1 = VK_mL[0];
					line2 =	VK_mL[1];
				} else if (i == 2) {
					line1 = VK_mL[2];
					line2 =	VK_mL[1];
				} else if (i == 3) {
					line1 = VK_mL[2];
					line2 =	VK_mL[3];
				}

				line2.




				if ((i == 0)||(i == 1)) {
					QLineF l = VK_mL[0];
				} else {
					QLineF l = VK_mL[2];
				}


				QLineF l1 = VK_mL[0];
				QLineF l2 = VK_mL[2];

				QPointF pnt1 = VK_normalPoint(l1.x1(), l1.y1(), l1.x2(), l1.y2(), wall.VK_mCenter.rx(), wall.VK_mCenter.ry());
				QPointF pnt2 = VK_normalPoint(l2.x1(), l2.y1(), l2.x2(), l2.y2(), wall.VK_mCenter.rx(), wall.VK_mCenter.ry());

				QLineF len1 (wall.VK_mCenter, pnt1);
				QLineF len2 (wall.VK_mCenter, pnt2);




				QLineF l;
				if (len1 < len2) {
					l = len1;
				} else {
					l = len2;
				}

				QVector2D n(l.p2() - l.p1());
				QVector2D n2 = n.normalized() * wall.width();
			}
		}
		*/



	QLineF tmpLine;
	for(int i = 0; i<4; i++)
	{
		QLineF l = wall.VK_getLine(i);
		QPainterPath p(l.p1());
		p.lineTo(l.p2());

		if (boundingRegion.intersects(p)){
			tmpLine = l;
		}
	}
	return tmpLine;

}

QLineF D2RobotModel::VK_intersectRobotLine(WallItem& wall)
{
	for(int i = 0; i < 4; i++)
	{
		QPainterPath p(VK_mL[i].p1());
		p.lineTo(VK_mL[i].p2());
		if (wall.VK_mWallPath.intersects(p)) {
			return VK_mL[i];
		}
	}
}

QLineF D2RobotModel::VK_interWallLine(WallItem& wall)
{
	if (wall.isCircle()) {
		for(int i = 0; i < 4; i++) {
			QPainterPath p(VK_mL[i].p1());
			p.lineTo(VK_mL[i].p2());
			if (wall.VK_mWallPath.intersects(p)) {
				QLineF l = VK_mL[i];
				QPointF pntIntersect = VK_normalPoint(l.x1(), l.y1(), l.x2(), l.y2(), wall.VK_mCenter.rx(), wall.VK_mCenter.ry());
				QVector2D n(pntIntersect - wall.VK_mCenter);
				QLineF lineN(pntIntersect , wall.VK_mCenter);
				n = n.normalized();
				QPointF pntInter = wall.VK_mCenter + n.toPointF()*wall.width();
				lineN = lineN.normalVector();
				QPointF p2(lineN.p1() - lineN.p2());
				return QLineF(pntInter, pntInter + p2);
			}
		}
	} else {
		QLineF tmpLine;
		QGraphicsLineItem *l = new QGraphicsLineItem(0,0,0,0);
		for(int i = 0; i<4; i++) {
			l->setLine(VK_mL[i]);
			if (wall.collidesWithItem(l)){
				tmpLine = l->line();
				VK_setEdgeWall(i, &wall);
			}
		}
		return tmpLine;
	}
}

bool D2RobotModel::VK_isCollision(WallItem& wall, int i)
{
	return (wall.VK_mWallPath.contains(VK_mP[i]));
}

bool D2RobotModel::VK_isEdgeCollision(WallItem& wall, int i)
{
	QPainterPath path (VK_mL[i].p1() );
	path.lineTo(VK_mL[i].p2());
	return (wall.VK_mWallPath.intersects(path));
}

void D2RobotModel::VK_nextStep()
{
	QVector2D curPos(mPos);
	curPos += VK_getV() * Timeline::timeInterval;
	mPos = QPointF(curPos.x(), curPos.y());
	qreal timeInterval = Timeline::timeInterval;
	mAngle += VK_mAngularVelocity * timeInterval;
	VK_updateCoord();
}

void D2RobotModel::recalculateParams()
{

	// do nothing until robot gets back on the ground
	if (!mD2ModelWidget->isRobotOnTheGround()) {
		mNeedSync = true;
		return;
	}
	synchronizePositions();
	for (int i = 0; i < 4; i++)
	{
		VK_setWall(i, NULL);
		VK_setEdgeWall(i, NULL);
	}

	for (int i = 0; i < mWorldModel.mWalls.length(); i++) { // Ищет коллизии
		VK_checkCollision(*(mWorldModel.mWalls[i]));
		VK_updateCoord();
	}

	// Считаем изменение скорости и угла
	countNewCoord();



	getFromWalls();


	VK_nextStep(); //Сдвигаем и поварачиваем
	countMotorTurnover();

}

void D2RobotModel::getFromWalls()
{
	for (int i = 0; i < 4; i++){ // Вытаскиваем из стен
		for (int j = 0; j < mWorldModel.mWalls.length(); j++) {
			if (VK_isCollision(*(mWorldModel.mWalls[j]), i)) {
				VK_getRobotFromWall(*(mWorldModel.mWalls[j]), i);
			}
			if (VK_isEdgeCollision(*(mWorldModel.mWalls[j]), i)) {
				if (VK_mRobotEdgeWalls[i] != NULL) {
					VK_getEdgeRobotFromWall(*(mWorldModel.mWalls[j]), i);
				}
			}
		}
	}
}

void D2RobotModel::nextFragment()
{
	if (!mD2ModelWidget->isRobotOnTheGround()) {
		return;
	}
	synchronizePositions();
	countBeep();
	mD2ModelWidget->draw(mPos, mAngle);
	mNeedSync = true;
}

void D2RobotModel::synchronizePositions()
{
	if (mNeedSync) {
		mPos = mD2ModelWidget->robotPos();
		mNeedSync = false;
	}
}

void D2RobotModel::showModelWidget()
{
	mD2ModelWidget->init(true);
}

void D2RobotModel::setRotation(qreal angle)
{
	mPos = mD2ModelWidget ? mD2ModelWidget->robotPos() : QPointF(0, 0);
	mAngle = fmod(angle, 360);
	mD2ModelWidget->draw(mPos, mAngle);
}

double D2RobotModel::rotateAngle() const
{
	return mAngle;
}

void D2RobotModel::setSpeedFactor(qreal speedMul)
{
	mSpeedFactor = speedMul;
	mTimeline->setSpeedFactor(speedMul);
}

QPointF D2RobotModel::robotPos()
{
	return mPos;
}

void D2RobotModel::serialize(QDomDocument &target)
{
	QDomElement robot = target.createElement("robot");
	robot.setAttribute("position", QString::number(mPos.x()) + ":" + QString::number(mPos.y()));
	robot.setAttribute("direction", mAngle);
	configuration().serialize(robot, target);
	target.firstChildElement("root").appendChild(robot);
}

void D2RobotModel::deserialize(QDomElement const &robotElement)
{
	QString const positionStr = robotElement.attribute("position", "0:0");
	QStringList const splittedStr = positionStr.split(":");
	qreal const x = static_cast<qreal>(splittedStr[0].toDouble());
	qreal const y = static_cast<qreal>(splittedStr[1].toDouble());
	mPos = QPointF(x, y);
	mAngle = robotElement.attribute("direction", "0").toDouble();
	configuration().deserialize(robotElement);
	mNeedSync = false;
	nextFragment();
}

Timeline *D2RobotModel::timeline() const
{
	return mTimeline;
}

details::NxtDisplay *D2RobotModel::display()
{
	return mDisplay;
}

void D2RobotModel::setNoiseSettings()
{
	mNeedSensorNoise = SettingsManager::value("enableNoiseOfSensors").toBool();
	mNeedMotorNoise = SettingsManager::value("enableNoiseOfMotors").toBool();
	mNoiseGen.setApproximationLevel(SettingsManager::value("approximationLevel").toUInt());
}

int D2RobotModel::truncateToInterval(int const a, int const b, int const res) const
{
	return (res >= a && res <= b) ? res : (res < a ? a : b);
}
