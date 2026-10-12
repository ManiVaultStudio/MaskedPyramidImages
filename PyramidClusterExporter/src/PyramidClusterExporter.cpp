#include "PyramidClusterExporter.h"

#include <actions/PluginTriggerAction.h>

#include <ClusterData/ClusterData.h>

#include <filesystem>
#include <vector>

#include <fmt/base.h>
#include <rapidcsv.h>

Q_PLUGIN_METADATA(IID "studio.manivault.PyramidClusterExporter")

using namespace mv;
using namespace mv::gui;

// =============================================================================
// Dialog
// =============================================================================
namespace
{
    std::vector<std::string> toColumn(const QVector<Cluster>& clusters, size_t rowCount)
    {
        std::vector<std::string> col(rowCount);

        for (const auto& cluster : clusters) {
            const auto value = cluster.getName().toStdString();
            for (const uint32_t r : cluster.getIndices()) {
                col[r] = value;
            }
        }
        return col;
    }

    void saveOverwrite(rapidcsv::Document& doc, const std::filesystem::path& target)
    {
        std::filesystem::path tmp = target;
        tmp += ".tmp";

        try {
            doc.Save(tmp.string());
            std::filesystem::rename(tmp, target);   // replaces target
        }
        catch (...) {
            std::error_code ec;
            std::filesystem::remove(tmp, ec);               // cleanup
            throw;
        }
    }
}

// =============================================================================
// Dialog
// =============================================================================

PyramidClusterExporterDialog::PyramidClusterExporterDialog(QWidget* parent) : 
    QDialog(parent),
    _datafilePickerAction(this, "CSV file"),
    _columnNameAction(this, "Column name", "ID"),
    _writeButtonAction(this, "OK"),
    _groupAction(this, "Settings")
{
    setWindowTitle(tr("CSV ID export"));

    _datafilePickerAction.setNameFilters( { QStringLiteral("CSV file (*.csv)") } );
    _datafilePickerAction.setUseNativeFileDialog(true);
    _datafilePickerAction.setPlaceHolderString("Pick CSV file...");
    _datafilePickerAction.setFileType("CSV file");
    _datafilePickerAction.setToolTip("CSV file to append the cluster IDs");

    _groupAction.addAction(&_datafilePickerAction);
    _groupAction.addAction(&_columnNameAction);
    _groupAction.addAction(&_writeButtonAction);

    auto layout = new QVBoxLayout();

    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(_groupAction.createWidget(this));

    setLayout(layout);

    connect(&_writeButtonAction, &TriggerAction::triggered, this, &QDialog::accept);
}

// =============================================================================
// Plugin
// =============================================================================

PyramidClusterExporter::PyramidClusterExporter(const mv::plugin::PluginFactory* factory) :
    WriterPlugin(factory)
{
}

void PyramidClusterExporter::init()
{
}

void PyramidClusterExporter::writeData()
{
    // Let the user select one of those data sets
    PyramidClusterExporterDialog outputDialog(nullptr);
    
    outputDialog.setModal(true);

    const int ok = outputDialog.exec();

    if (ok != QDialog::Accepted) {
        fmt::println("PyramidClusterExporter: No data written to disk.");
        return;
    }

    const QVector<Cluster>& clusters = getInputDataset<Clusters>()->getClusters();
    const int64_t numClusters = static_cast<int64_t>(clusters.size());

    size_t numClusterIDs = 0;
    for (int64_t numCluster = 0; numCluster < numClusters; ++numCluster) {
        numClusterIDs += clusters[numCluster].getNumberOfIndices();
    }

    // Read CSV file and write back to it
    try {
        const std::string csvPath = outputDialog.getFilePath().toStdString();
        const std::string colName = outputDialog.getColumnName().toStdString();

        fmt::println("PyramidClusterExporter: Reading: {}", csvPath);
        rapidcsv::Document doc(csvPath);
        
        // TODO: check that column headers exist
        //const std::vector<std::string> columnNames = doc.GetColumnNames(); 
        const auto numDataRows = doc.GetRowCount();

        if (numClusterIDs != numDataRows)
            fmt::println("PyramidClusterExporter: Mismatch!");

        const std::vector<std::string> clusterIDs = toColumn(clusters, numDataRows);

        fmt::println("PyramidClusterExporter: Writing: {}", csvPath);
        doc.InsertColumn(doc.GetColumnCount(), clusterIDs, colName);
        saveOverwrite(doc, csvPath);

        fmt::println("PyramidClusterExporter: Done.");
    }
    catch (const std::ios_base::failure& e) {
        fmt::println("PyramidClusterExporter: Error, could not read the csv file: {}", e.what());
    }
    catch (const std::exception& e) {
        fmt::println("PyramidClusterExporter: Error: {}", e.what());
    }

    return;
}


// =============================================================================
// Factory
// =============================================================================

PyramidClusterExporterFactory::PyramidClusterExporterFactory()
{
    WriterPluginFactory::setIconByName("database");
}

mv::plugin::WriterPlugin* PyramidClusterExporterFactory::produce()
{
    return new PyramidClusterExporter(this);
}

DataTypes PyramidClusterExporterFactory::supportedDataTypes() const
{
    DataTypes supportedTypes;
    supportedTypes.append(ClusterType);
    return supportedTypes;
}

PluginTriggerActions PyramidClusterExporterFactory::getPluginTriggerActions(const mv::Datasets& datasets) const
{
    PluginTriggerActions pluginTriggerActions;

    const auto getPluginInstance = [this](const Dataset<Clusters>& dataset) -> PyramidClusterExporter* {
        return dynamic_cast<PyramidClusterExporter*>(plugins().requestPlugin(getKind(), { dataset }));
    };


    if (datasets.size() == 1 && PluginFactory::areAllDatasetsOfTheSameType(datasets, ClusterType)) {
        auto pluginTriggerAction = new PluginTriggerAction(const_cast<PyramidClusterExporterFactory*>(this), this, "Cluster ID (append to CSV)", "Append Cluster ID to CSV file", icon(), [this, getPluginInstance, datasets](PluginTriggerAction& pluginTriggerAction) -> void {
            for (const auto& dataset : datasets)
                getPluginInstance(dataset);
        });

        pluginTriggerActions << pluginTriggerAction;
    }

    return pluginTriggerActions;
}
