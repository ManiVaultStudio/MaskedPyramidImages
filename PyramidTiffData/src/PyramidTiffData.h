#pragma once
#include "pyramidtiffdata_export.h"

#include "OmeTiffPyramid.h"
#include "PolygonData.h"

#include <event/EventListener.h>

#include <RawData.h>
#include <Set.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <QSharedPointer>
#include <QString>

class PyramidInfoAction;

// TODO:
// - fix: mapping from low level (4) to derived data (PCA) of high level (3) does not work 

// =============================================================================
// Data (Raw)
// =============================================================================

const auto PyramidImageDataType = mv::DataType(QString("Masked Image Pyramid"));

class PYRAMIDTIFFDATA_EXPORT PyramidImageData : public mv::plugin::RawData
{
    Q_OBJECT
public:
    explicit PyramidImageData(const mv::plugin::PluginFactory* factory);
    ~PyramidImageData() override = default;
    PyramidImageData(const PyramidImageData&) = delete;
    PyramidImageData& operator=(const PyramidImageData&) = delete;
    PyramidImageData(PyramidImageData&&) = delete;
    PyramidImageData& operator=(PyramidImageData&&) = delete;

    void init() override;

    /**
     * Create dataset for raw data
     * @param guid Globally unique dataset identifier (use only for deserialization)
     * @return Smart pointer to dataset
     */
    [[nodiscard]] mv::Dataset<mv::DatasetImpl> createDataSet(const QString& guid = "") const override;

    /**
     * Load tiff and json info
     * @param tiffFilePath path to tiff file
     * @param jsonFilePath path to json file
     * @return success
     */
    [[nodiscard]] bool scan(const QString& tiffFilePath, const QString& jsonFilePath);

public: // Getter
    const PyramidTiffData::OmeTiffPyramid& getPyramid() const { return _tiffPyramid;  }
    const PyramidTiffData::PolygonData& getPolygons() const { return _polygonMasks;  }

    std::uint64_t getNumPoints() const;
    std::uint64_t getNumDimensions() const;

public: // Serialization

    /**
     * Load widget action from variant
     * @param variantMap representation of the widget action
     */
    void fromVariantMap(const QVariantMap& variantMap) override;

    /**
     * Save widget action to variant
     * @return Variant representation of the widget action
     */
    QVariantMap toVariantMap() const override;

private:
    PyramidTiffData::OmeTiffPyramid _tiffPyramid = {};
    PyramidTiffData::PolygonData    _polygonMasks = {};
};

// =============================================================================
// Data (Set)
// =============================================================================

class PYRAMIDTIFFDATA_EXPORT PyramidImage : public mv::DatasetImpl
{
    Q_OBJECT
public:
	PyramidImage() = delete;
    explicit PyramidImage(const QString& dataName, const bool mayUnderive = true, const QString& guid = "");

    ~PyramidImage() override = default;

    PyramidImage(const PyramidImage&) = delete;
    PyramidImage& operator=(const PyramidImage&) = delete;
    PyramidImage(PyramidImage&&) = delete;
    PyramidImage& operator=(PyramidImage&&) = delete;

    /**
     * Get a copy of the dataset
     * @return Smart pointer to copy of dataset
     */
    mv::Dataset<DatasetImpl> copy() const override
    {
        auto pyramidImageData = new PyramidImage(getRawDataName());

        pyramidImageData->setText(text());

        return { pyramidImageData };
    }

    /**
     * Create subset from the current selection and specify where the subset will be placed in the data hierarchy
     * @param guiName Name of the subset in the GUI
     * @param parentDataSet Smart pointer to parent dataset in the data hierarchy (default is below the set)
     * @param visible Whether the subset will be visible in the UI
     * @return Smart pointer to the created subset
     */
    mv::Dataset<DatasetImpl> createSubsetFromSelection(const QString& guiName, const mv::Dataset<DatasetImpl>& parentDataSet = mv::Dataset<DatasetImpl>(), const bool& visible = true) const override {
        return mv::data().createSubsetFromSelection(getSelection(), toSmartPointer(), guiName, parentDataSet, visible);
    }

    void init() override;

    void scan() const;

    void read_level();

public: // Getter and Setter
    [[nodiscard]] QString getTiffFilePath() const {
        return _tiffFilePath;
    }

    void setTiffFilePath(const QString& path) {
        _tiffFilePath = path;
    }

    [[nodiscard]] QString getJsonFilePath() const {
        return _jsonFilePath;
    }

    void setJsonFilePath(const QString& path) {
        _jsonFilePath = path;
    }

    std::uint64_t getNumPoints() const {
        return getRawData<PyramidImageData>()->getNumPoints();
    }

    std::uint64_t getNumDimensions() const {
        return getRawData<PyramidImageData>()->getNumDimensions();
    }

public: // Selection

    /**
     * Get selection
     * @return Selection indices
     */
    std::vector<std::uint32_t>& getSelectionIndices() override;

    /**
     * Select by indices
     * @param selectionIndices Selection indices
     */
    void setSelectionIndices(const std::vector<std::uint32_t>& selectionIndices) override;

    /**
     * Select by indices
     * @param selectionIndices Selection indices
     */
    void setSelectionIndices(std::vector<std::uint32_t>&& selectionIndices);

    /** Determines whether items can be selected */
    bool canSelect() const override;

    /** Determines whether all items can be selected */
    bool canSelectAll() const override;

    /** Determines whether there are any items which can be deselected */
    bool canSelectNone() const override;

    /** Determines whether the item selection can be inverted (more than one) */
    bool canSelectInvert() const override;

    /** Select all items */
    void selectAll() override;

    /** Deselect all items */
    void selectNone() override;

    /** Invert item selection */
    void selectInvert() override;

    void selectionMapping(const mv::Dataset<>& input);

public:
    std::vector<std::uint32_t> selection = {};

public: // Serialization

    /**
     * Load widget action from variant
     * @param variantMap representation of the widget action
     */
    void fromVariantMap(const QVariantMap& variantMap) override;

    /**
     * Save widget action to variant
     * @return Variant representation of the widget action
     */
    QVariantMap toVariantMap() const override;

private:
    static const QString SID_levelDatasets;
    static const QString SID_tiffFilePath;
    static const QString SID_jsonFilePath;

private:
    QString _tiffFilePath = {};
    QString _jsonFilePath = {};
    QSharedPointer<PyramidInfoAction> _infoAction = {};                                     /** Shared pointer to info action */
    std::unordered_map<QString, std::pair<mv::Dataset<>, uint32_t>> _levelDatasets = {};     /** Helper data set for selection */
    std::unordered_map<QString, uint8_t> _selectionCounter = {};
    mv::EventListener _eventListener = {};                                                  /** Listen to ManiVault events */
};

// =============================================================================
// Factory
// =============================================================================

class PyramidImageDataFactory : public mv::plugin::RawDataFactory
{
    Q_INTERFACES(mv::plugin::RawDataFactory mv::plugin::PluginFactory)
        Q_OBJECT
        Q_PLUGIN_METADATA(IID   "studio.manivault.PyramidImageData"
                          FILE  "PluginInfo.json")

public:
    PyramidImageDataFactory();
    ~PyramidImageDataFactory() override = default;
    PyramidImageDataFactory(const PyramidImageDataFactory&) = delete;
    PyramidImageDataFactory& operator=(const PyramidImageDataFactory&) = delete;
    PyramidImageDataFactory(PyramidImageDataFactory&&) = delete;
    PyramidImageDataFactory& operator=(PyramidImageDataFactory&&) = delete;

    mv::plugin::RawData* produce() override;
};