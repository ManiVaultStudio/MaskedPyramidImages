#pragma once

#include <actions/DatasetPickerAction.h>
#include <actions/GroupAction.h>
#include <actions/FilePickerAction.h>
#include <actions/StringAction.h>
#include <actions/TriggerAction.h>

#include <LoaderPlugin.h>

#include <QDialog>

// =============================================================================
// Loading input box
// =============================================================================

class PyramidTiffLoader;

struct PyramidTiffSettings
{
	QString name = {};
	QString tiffFilePath = {};
	QString jsonFilePath = {};
};

class PyramidTiffLoaderDialog : public QDialog
{
    Q_OBJECT

public:
    PyramidTiffLoaderDialog() = delete;
    explicit PyramidTiffLoaderDialog(QWidget* parent, PyramidTiffLoader* pyramidLoader, const QString& filePath);

    ~PyramidTiffLoaderDialog() override = default;

    PyramidTiffLoaderDialog(const PyramidTiffLoaderDialog&) = delete;
    PyramidTiffLoaderDialog& operator=(const PyramidTiffLoaderDialog&) = delete;
    PyramidTiffLoaderDialog(PyramidTiffLoaderDialog&&) = delete;
    PyramidTiffLoaderDialog& operator=(PyramidTiffLoaderDialog&&) = delete;

public:
    /** Get preferred size */
    [[nodiscard]] QSize sizeHint() const override {
        return { 400, 50 };
    }

    /** Get minimum size hint*/
    [[nodiscard]] QSize minimumSizeHint() const override {
        return { 300, 50 };
    }

    /** Get the GUI name of the loaded dataset */
    [[nodiscard]] QString getDatasetName() const {
        return _datasetNameAction.getString();
    }

    [[nodiscard]] QString getTiffFilePath() const {
        return _inputTiffFilePickerAction.getFilePath();
    }

    [[nodiscard]] QString getJsonFilePath() const {
        return _inputJsonFilePickerAction.getFilePath();
    }

public:
    void setDatasetName(const QString& name) {
        _datasetNameAction.setString(name);
    }

    void setTiffFilePath(const QString& path) {
        _inputTiffFilePickerAction.setFilePath(path);
    }

    void setJsonFilePath(const QString& path) {
        _inputJsonFilePickerAction.setFilePath(path);
    }

    void enableLoadButton(const bool enable) {
        _loadAction.setEnabled(enable);
    }

private: 
    void enableLoad();

protected:
    mv::gui::StringAction           _datasetNameAction;             /** Dataset name action */
    mv::gui::FilePickerAction       _inputTiffFilePickerAction;
    mv::gui::FilePickerAction       _inputJsonFilePickerAction;
    mv::gui::TriggerAction          _loadAction;                    /** Load action */
    mv::gui::GroupAction            _groupAction;                   /** Group action */
};

// =============================================================================
// View
// =============================================================================

class PyramidTiffLoader : public mv::plugin::LoaderPlugin
{
    Q_OBJECT
public:
    PyramidTiffLoader() = delete;
    explicit PyramidTiffLoader(const mv::plugin::PluginFactory* factory) : mv::plugin::LoaderPlugin(factory) { }
    ~PyramidTiffLoader() override = default;
    PyramidTiffLoader(const PyramidTiffLoader&) = delete;
    PyramidTiffLoader& operator=(const PyramidTiffLoader&) = delete;
    PyramidTiffLoader(PyramidTiffLoader&&) = delete;
    PyramidTiffLoader& operator=(PyramidTiffLoader&&) = delete;

    void init() override;

    void loadData() Q_DECL_OVERRIDE;

public:
    void setPyramidTiffSettings(PyramidTiffSettings pyramidTiffSettings){
        _pyramidTiffSettings = std::move(pyramidTiffSettings);
    }

private:
    PyramidTiffSettings     _pyramidTiffSettings;
};

// =============================================================================
// Factory
// =============================================================================

class PyramidTiffLoaderFactory : public mv::plugin::LoaderPluginFactory
{
    Q_INTERFACES(mv::plugin::LoaderPluginFactory mv::plugin::PluginFactory)
    Q_OBJECT
    Q_PLUGIN_METADATA(IID   "studio.manivault.PyramidTiffLoader"
                      FILE  "PluginInfo.json")

public:
    PyramidTiffLoaderFactory();
    ~PyramidTiffLoaderFactory() override = default;
    PyramidTiffLoaderFactory(const PyramidTiffLoaderFactory&) = delete;
    PyramidTiffLoaderFactory& operator=(const PyramidTiffLoaderFactory&) = delete;
    PyramidTiffLoaderFactory(PyramidTiffLoaderFactory&&) = delete;
    PyramidTiffLoaderFactory& operator=(PyramidTiffLoaderFactory&&) = delete;

    mv::plugin::LoaderPlugin* produce() override;

    mv::DataTypes supportedDataTypes() const override;
};