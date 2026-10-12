#pragma once

#include <WriterPlugin.h>

#include <actions/GroupAction.h>
#include <actions/FilePickerAction.h>
#include <actions/StringAction.h>
#include <actions/TriggerAction.h>

#include <QDialog>

// =============================================================================
// Dialog
// =============================================================================

class PyramidClusterExporterDialog : public QDialog
{
    Q_OBJECT

public:
    explicit PyramidClusterExporterDialog(QWidget* parent);

public:
    QString getFilePath() const {
        return _datafilePickerAction.getFilePath();
    }
    QString getColumnName() const {
        return _columnNameAction.getString();
    }

private:
    mv::gui::FilePickerAction       _datafilePickerAction;             /** Path to CSV file on disk */
    mv::gui::StringAction           _columnNameAction;                 /** Name of cluster ID column */
    mv::gui::TriggerAction          _writeButtonAction;
    mv::gui::GroupAction            _groupAction;
};

// =============================================================================
// Plugin
// =============================================================================

class PyramidClusterExporter : public mv::plugin::WriterPlugin
{
    Q_OBJECT
public:
    explicit PyramidClusterExporter(const mv::plugin::PluginFactory* factory);
    PyramidClusterExporter(const PyramidClusterExporter&) = delete;
    PyramidClusterExporter(PyramidClusterExporter&&) = delete;
    PyramidClusterExporter& operator=(const PyramidClusterExporter&) = delete;
    PyramidClusterExporter& operator=(PyramidClusterExporter&&) = delete;
    ~PyramidClusterExporter(void) override = default;

    void init() override;

    void writeData() Q_DECL_OVERRIDE;

};


// =============================================================================
// Factory
// =============================================================================

class PyramidClusterExporterFactory : public mv::plugin::WriterPluginFactory
{
    Q_INTERFACES(mv::plugin::WriterPluginFactory mv::plugin::PluginFactory)
        Q_OBJECT
        Q_PLUGIN_METADATA(IID   "studio.manivault.PyramidClusterExporter"
                          FILE  "PluginInfo.json")

public:
    PyramidClusterExporterFactory();
    PyramidClusterExporterFactory(const PyramidClusterExporterFactory&) = delete;
    PyramidClusterExporterFactory(PyramidClusterExporterFactory&&) = delete;
    PyramidClusterExporterFactory& operator=(const PyramidClusterExporterFactory&) = delete;
    PyramidClusterExporterFactory& operator=(PyramidClusterExporterFactory&&) = delete;
    ~PyramidClusterExporterFactory(void) override = default;

    mv::plugin::WriterPlugin* produce() override;

    mv::DataTypes supportedDataTypes() const override;

    /**
     * Get plugin trigger actions given \p datasets
     * @param datasets Vector of input datasets
     * @return Vector of plugin trigger actions
     */
    mv::gui::PluginTriggerActions getPluginTriggerActions(const mv::Datasets& datasets) const override;
};