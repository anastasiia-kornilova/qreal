#include "robotsGeneratorPluginBase.h"

#include <qrutils/inFile.h>

using namespace qReal::robots::generators;

RobotsGeneratorPluginBase::RobotsGeneratorPluginBase()
{
	mAppTranslator.load(":/robotsGeneratorBase_" + QLocale::system().name());
	QApplication::installTranslator(&mAppTranslator);
}

QFileInfo RobotsGeneratorPluginBase::srcPath()
{
	Id const &activeDiagram = mMainWindowInterface->activeDiagram();

	QString const projectName = "example" + QString::number(mCurrentCodeNumber);
	QFileInfo fileInfo = defaultFilePath(projectName);
	QList<QFileInfo> pathsList = mCodePath.values(activeDiagram);
	bool newCode = true;

	if (!pathsList.isEmpty()) {
			QString const ext = extension();

			foreach(QFileInfo const &path, pathsList) {
					if (mTextManager->isDefaultPath(path.absoluteFilePath())
					&& (!mTextManager->isModifiedEver(path.absoluteFilePath()))
					&& !path.completeSuffix().compare(ext)) {
							fileInfo = path;
							newCode = false;
							break;
					}
			}
	}

	if (newCode) {
		mCurrentCodeNumber++;
	}

	return fileInfo;
}

void RobotsGeneratorPluginBase::init(PluginConfigurator const &configurator)
{
	mCurrentCodeNumber = 0;
	mProjectManager = &configurator.projectManager();
	mSystemEvents = &configurator.systemEvents();
	mTextManager = &configurator.textManager();

	mMainWindowInterface = &configurator.mainWindowInterpretersInterface();
	mRepo = dynamic_cast<qrRepo::RepoApi const *>(&configurator.logicalModelApi().logicalRepoApi());
	mProjectManager = &configurator.projectManager();

	mTextManager->addExtension(extension(), extDescrition());

	connect(mSystemEvents, SIGNAL(codePathChanged(qReal::Id, QFileInfo, QFileInfo))
			, this, SLOT(regenerateCode(qReal::Id, QFileInfo, QFileInfo)));
	connect(mSystemEvents, SIGNAL(newCodeAppeared(qReal::Id, QFileInfo)), this, SLOT(addNewCode(qReal::Id, QFileInfo)));
	connect(mSystemEvents, SIGNAL(diagramClosed(qReal::Id)), this, SLOT(removeDiagram(qReal::Id)));
	connect(mSystemEvents, SIGNAL(codeTabClosed(QFileInfo)), this, SLOT(removeCode(QFileInfo)));
}

bool RobotsGeneratorPluginBase::generateCode()
{
	mProjectManager->save();
	mMainWindowInterface->errorReporter()->clearErrors();

	MasterGeneratorBase * const generator = masterGenerator();
	QFileInfo path = srcPath();

	generator->initialize();
	generator->setProjectDir(path);

	QString const generatedSrcPath = generator->generate();
	if (mMainWindowInterface->errorReporter()->wereErrors()) {
		delete generator;
		return false;
	}


	QString const generatedCode = utils::InFile::readAll(generatedSrcPath);
	if (!generatedCode.isEmpty()) {
		mMainWindowInterface->showInTextEditor(path);
	}

	delete generator;
	return true;
}

void RobotsGeneratorPluginBase::regenerateCode(qReal::Id const &diagram, QFileInfo const &oldFileInfo, QFileInfo const &newFileInfo)
{
	if (!oldFileInfo.completeSuffix().compare(extension())) {
		mTextManager->changeFilePath(oldFileInfo.absoluteFilePath(), newFileInfo.absoluteFilePath());

		mCodePath.remove(diagram, oldFileInfo);

		mCodePath.insert(diagram, newFileInfo);

		regenerateExtraFiles(newFileInfo);
	}
}

void RobotsGeneratorPluginBase::addNewCode(Id const &diagram, QFileInfo const &fileInfo)
{
	mCodePath.insert(diagram, fileInfo);
}

void RobotsGeneratorPluginBase::removeDiagram(qReal::Id const &diagram)
{
	mCodePath.remove(diagram);
}

void RobotsGeneratorPluginBase::removeCode(QFileInfo const &fileInfo)
{
	Id const &diagram = mCodePath.key(fileInfo);

	mCodePath.remove(diagram, fileInfo);
}
