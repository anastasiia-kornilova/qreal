#include "interpreterTest.h"

#include <src/interpreter/interpreter.h>

#include <interpreterBase/robotModel/configurationInterfaceMock.h>

using namespace qrTest::robotsTests::interpreterCoreTests;

using namespace interpreterCore::interpreter;
using namespace ::testing;

void InterpreterTest::SetUp()
{
	mQrguiFacade.reset(new QrguiFacade("unittests/testModel.qrs"));

	DummyBlockFactory *blocksFactory = new DummyBlockFactory();

	mFakeConnectToRobotAction.reset(new QAction(nullptr));

	ConfigurationInterfaceMock configurationInterfaceMock;

	ON_CALL(mModel, needsConnection()).WillByDefault(Return(false));

	ON_CALL(mModel, init()).WillByDefault(Invoke([&] {
		QMetaObject::invokeMethod(&mModel, "connected", Q_ARG(bool, true));
		QMetaObject::invokeMethod(&mModel.configuration(), "allDevicesConfigured");
	}));

	ON_CALL(mModel, configuration()).WillByDefault(ReturnRef(configurationInterfaceMock));

	EXPECT_CALL(mModel, configuration()).Times(AtLeast(1));
	EXPECT_CALL(mModel, needsConnection()).Times(AtLeast(1));
	EXPECT_CALL(mModel, init()).Times(AtLeast(1));

	mInterpreter.reset(new Interpreter(
			mQrguiFacade->graphicalModelAssistInterface()
			, mQrguiFacade->logicalModelAssistInterface()
			, mQrguiFacade->mainWindowInterpretersInterface()
			, mQrguiFacade->projectManagementInterface()
			, blocksFactory
			, &mModel
			, *mFakeConnectToRobotAction
			));
}

TEST_F(InterpreterTest, interpret)
{
	mInterpreter->interpret();
}

TEST_F(InterpreterTest, stopRobot)
{
	EXPECT_CALL(mModel, stopRobot()).Times(1);

	mInterpreter->interpret();
	mInterpreter->stopRobot();
}