#include "PyramidTiffLoader.h"

#include <PyramidTiffData.h>

#include <Application.h>
#include <Set.h>

#include <QDir>
#include <QFileInfo>
#include <QMainWindow>

Q_PLUGIN_METADATA(IID QStringLiteral(u"studio.manivault.PyramidTiffLoader"))

using namespace mv;

namespace
{
    inline const QString TIFF_FILE_FILTER = QStringLiteral(u"Tiff Files (*.tif *.tiff *.ome.tif *.ome.tiff)");
    inline const QString JSON_FILE_FILTER = QStringLiteral(u"JSON Files (*.json *.geojson)");
}

// =============================================================================
// Dialog
// =============================================================================

PyramidTiffLoaderDialog::PyramidTiffLoaderDialog(QWidget* parent, PyramidTiffLoader* pyramidLoader, const QString& filePath) :
    QDialog(parent),
    _datasetNameAction(this, QStringLiteral(u"Dataset name"), QStringLiteral(u"")),
    _inputTiffFilePickerAction(this, QStringLiteral(u"Tiff file:")),
    _inputJsonFilePickerAction(this, QStringLiteral(u"JSON file:")),
    _loadAction(this, QStringLiteral(u"Load")),
    _groupAction(this, QStringLiteral(u"Settings"))
{
    setWindowTitle(QStringLiteral(u"Masked Image Pyramid Loader"));

    _inputTiffFilePickerAction.setNameFilters({ TIFF_FILE_FILTER });
    _inputJsonFilePickerAction.setNameFilters({ JSON_FILE_FILTER });

    _groupAction.addAction(&_datasetNameAction);
    _groupAction.addAction(&_inputTiffFilePickerAction);
    _groupAction.addAction(&_inputJsonFilePickerAction);
    _groupAction.addAction(&_loadAction);

    auto layout = new QVBoxLayout();

    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(_groupAction.createWidget(this));

    setLayout(layout);

    const QFileInfo fi(filePath);
    QString jsonFileNameCandidate = fi.dir().filePath(fi.completeBaseName() + ".json");
    bool candidateFileExists = QFileInfo::exists(jsonFileNameCandidate);

    if (!candidateFileExists) {
        jsonFileNameCandidate = fi.dir().filePath(fi.completeBaseName() + ".geojson");
        candidateFileExists = QFileInfo::exists(jsonFileNameCandidate);
    }

    setDatasetName(fi.baseName());
    setTiffFilePath(filePath);
    setJsonFilePath(candidateFileExists ? jsonFileNameCandidate : "");
    enableLoadButton(candidateFileExists);

    connect(&_inputTiffFilePickerAction, &gui::FilePickerAction::filePathChanged, this, &PyramidTiffLoaderDialog::enableLoad);
    connect(&_inputJsonFilePickerAction, &gui::FilePickerAction::filePathChanged, this, &PyramidTiffLoaderDialog::enableLoad);

    // Accept when the load action is triggered
    connect(&_loadAction, &gui::TriggerAction::triggered, this, [this, pyramidLoader]() {
        if (!pyramidLoader)
            return;

        pyramidLoader->setPyramidTiffSettings({
            .name = getDatasetName(),
            .tiffFilePath = getTiffFilePath(),
            .jsonFilePath = getJsonFilePath()
            });

        accept();
        });

}

void PyramidTiffLoaderDialog::enableLoad()
{
    enableLoadButton(QFileInfo::exists(getTiffFilePath()) && QFileInfo::exists(getJsonFilePath()));
}

// =============================================================================
// Loader
// =============================================================================

void PyramidTiffLoader::init()
{
    // do nothing
}

void PyramidTiffLoader::loadData()
{
    const QString filePath = AskForFileName(TIFF_FILE_FILTER);

    // Don't try to load a file if the dialog was cancelled or the file name is empty
    if (filePath.isNull() || filePath.isEmpty())
        return;

    PyramidTiffLoaderDialog inputDialog(nullptr, this, filePath); // Application::getMainWindow()
    inputDialog.setModal(true);

    // open dialog and wait for user input
    if (inputDialog.exec() == QDialog::Accepted && !inputDialog.getTiffFilePath().isEmpty()) {

        auto pyramidImageData = mv::data().createDataset<PyramidImage>("Masked Image Pyramid", _pyramidTiffSettings.name);

    	pyramidImageData->setTiffFilePath(_pyramidTiffSettings.tiffFilePath);
        pyramidImageData->setJsonFilePath(_pyramidTiffSettings.jsonFilePath);

        pyramidImageData->scan();

        events().notifyDatasetDataChanged(pyramidImageData);
        pyramidImageData->getDataHierarchyItem().select();
    }
}

// =============================================================================
// Factory
// =============================================================================

PyramidTiffLoaderFactory::PyramidTiffLoaderFactory()
{
    LoaderPluginFactory::setIconByName(QStringLiteral(u"database"));
}

plugin::LoaderPlugin* PyramidTiffLoaderFactory::produce()
{
    return new PyramidTiffLoader(this);
}

DataTypes PyramidTiffLoaderFactory::supportedDataTypes() const
{
    return { PyramidImageDataType };
}
