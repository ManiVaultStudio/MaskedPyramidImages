#include "PyramidTiffData.h"

#include "PyramidInfoAction.h"

#include <event/Event.h>

#include <ImageData/Images.h>
#include <PointData/PointData.h>
#include <ClusterData/ClusterData.h>

#include <fmt/base.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <string>

Q_PLUGIN_METADATA(IID QStringLiteral(u"studio.manivault.PyramidImageData"))

using namespace mv;

// =============================================================================
// Data (Raw)
// =============================================================================

PyramidImageData::PyramidImageData(const plugin::PluginFactory* factory) :
    plugin::RawData(factory, PyramidImageDataType)
{
}

void PyramidImageData::init()
{
    // do nothing
}

mv::Dataset<mv::DatasetImpl> PyramidImageData::createDataSet(const QString& guid) const
{
    return { new PyramidImage(getName(), true, guid) };
}

bool PyramidImageData::scan(const QString& tiffFilePath, const QString& jsonFilePath)
{
    try {
        _tiffPyramid.init(tiffFilePath.toStdString());
        _tiffPyramid.print_info();
    }
    catch (const std::runtime_error& err)  {
        fmt::print("PyramidImageData::scan: cannot load tiff file: {}", err.what());
        return false;
    }

    try {
        _polygonMasks.init(jsonFilePath.toStdString(), _tiffPyramid.series().width, _tiffPyramid.series().height);
        _polygonMasks.print_info();
    }
    catch (const std::runtime_error& err) {
        fmt::print("PyramidImageData::scan: cannot parse json: {}", err.what());
        return false;
    }

    return true;
}

std::uint64_t PyramidImageData::getNumPoints() const
{
    if (_tiffPyramid.num_series() == 0)
        return 0;

    const auto& series = _tiffPyramid.series();

    return static_cast<std::uint64_t>(series.width) * series.height;
}

std::uint64_t PyramidImageData::getNumDimensions() const
{
    if (_tiffPyramid.num_series() == 0)
        return 0;

    const auto& series = _tiffPyramid.series();

    return static_cast<std::uint64_t>(series.channels);
}


void PyramidImageData::fromVariantMap(const QVariantMap& variantMap)
{
    plugin::RawData::fromVariantMap(variantMap);

    // Do NOT load these
	// since they depend on the existence of a file on disk
	// if that is available PyramidImage::fromVariantMap will
	// call PyramidImageData::scan and populate them
    //PyramidTiffData::OmeTiffPyramid _tiffPyramid = {};
    //PyramidTiffData::PolygonData    _polygonMasks = {};
}

QVariantMap PyramidImageData::toVariantMap() const
{
    auto variantMap = plugin::RawData::toVariantMap();

    // Do NOT save these (see fromVariantMap)
    //PyramidTiffData::OmeTiffPyramid _tiffPyramid = {};
	//PyramidTiffData::PolygonData    _polygonMasks = {};

    return variantMap;
}

// =============================================================================
// Data (Set)
// =============================================================================

const QString PyramidImage::SID_levelDatasets = QStringLiteral(u"LevelDatasets");
const QString PyramidImage::SID_tiffFilePath = QStringLiteral(u"TiffFilePath");
const QString PyramidImage::SID_jsonFilePath = QStringLiteral(u"JsonFilePath");

PyramidImage::PyramidImage(const QString& dataName, const bool mayUnderive, const QString& guid) :
    DatasetImpl(dataName, mayUnderive, guid)
{
}

void PyramidImage::init()
{
    DatasetImpl::init();

	setIconByName(QStringLiteral(u"square-caret-up"));

    _infoAction = QSharedPointer<PyramidInfoAction>::create(nullptr, *this);

    addAction(*_infoAction);

    connect(&_infoAction->getReadLevelAction(), &gui::TriggerAction::triggered, this, &PyramidImage::read_level);

    _eventListener.addSupportedEventType(static_cast<std::uint32_t>(mv::EventType::DatasetAboutToBeRemoved));
    _eventListener.addSupportedEventType(static_cast<std::uint32_t>(EventType::DatasetDataSelectionChanged));
    _eventListener.registerDataEventByType(PointType, [this](DatasetEvent* dataEvent) {

        const auto& datasetID = dataEvent->getDataset().getDatasetId();
        const auto itData = _levelDatasets.find(datasetID);

        switch (dataEvent->getType())
        {
        case EventType::DatasetAboutToBeRemoved:
        {

            if (itData != _levelDatasets.end())
                _levelDatasets.erase(itData);

            selectNone();

            break;
        }
        case EventType::DatasetDataSelectionChanged:
        {
            if (itData == _levelDatasets.end()) return;

            selectionMapping(dataEvent->getDataset());

            break;
        }
        default:
            break;
        }

        });

}

void PyramidImage::selectionMapping(const mv::Dataset<>& selectionInputData)
{
    if (_levelDatasets.size() <= 1)
        return;

    _selectionCount++;

    // return early if the current selection has already been handled
    // this plugin listens to selection notifications for all data in _levelDatasets
    // it then maps the selection from the input data to all other data in _levelDatasets
    // the core will then notify the plugin about those selection, but we do not need
    // to handle them anymore here. a counter prevents such recursive mapping
    if (_selectionCount >= _levelDatasets.size()) {
        _selectionCount = 0;
        return;
    }

    if (_selectionCount > 1) {
        return;
    }

    const auto fromLevelID = selectionInputData.getDatasetId();
    const auto& [selectionLevelData, fromLevel] = _levelDatasets.at(fromLevelID);
	assert(selectionLevelData.getDatasetId() == fromLevelID);

	const auto pyramidData = getRawData<PyramidImageData>();
    const auto& imagePyramid = pyramidData->getPyramid();
    const auto& levelInfos = imagePyramid.series().pyramid;

    const uint32_t baseWidth = levelInfos[0].width;
    const uint32_t baseHeigh = levelInfos[0].height;
    const uint32_t fromLevelWidth = levelInfos[fromLevel].width;
    const uint32_t fromLevelHeigh = levelInfos[fromLevel].height;

	// Map from level to base
    mv::Dataset<Points> selectionIDs = selectionInputData->getSelection();

    PyramidTiffData::sortAndUnique(selectionIDs->indices);

    auto baseIndices = (fromLevel == 0) ?
        selectionIDs->indices :
        PyramidTiffData::convertSelectionToUpscaled(selectionIDs->indices,
            fromLevelWidth, fromLevelHeigh, 
            baseWidth, baseHeigh);

    PyramidTiffData::sortAndUnique(baseIndices);

    // Map from base to all other levels
    for (const auto& [toLevelID, toLevelPair] : _levelDatasets)
    {
        auto& [toLevelData, toLevel] = toLevelPair;
        if (toLevelID == fromLevelID) continue;

        const uint32_t toLevelWidth = levelInfos[toLevel].width;
        const uint32_t toLevelHeigh = levelInfos[toLevel].height;

        // Map from base to level
        if (toLevel != fromLevel) {
            toLevelData->getSelection<Points>()->indices = 
                PyramidTiffData::convertSelectionToDownscaled(baseIndices,
                baseWidth, baseHeigh,
                toLevelWidth, toLevelHeigh);
        }
        else {
            toLevelData->getSelection<Points>()->indices = baseIndices;
        }

    	events().notifyDatasetDataSelectionChanged(toLevelData);

        // The ManiVault's core EventManager polls every ~20ms 
        // if a dataset is marked as containing a changed selection.
        // We need to mark the derived data of toLevelData to ensure
        // that they are updated in the same poll. Otherwise, they
        // will falsely be marked as already handled
        for (const auto& candidateDataset : mv::data().getAllDatasets()) {

            if (candidateDataset == selectionInputData ||
                candidateDataset == toLevelData)
                continue;

            const bool isDerived  = candidateDataset->isDerivedData() && candidateDataset->getSourceDataset<DatasetImpl>()->getRawDataName() == toLevelData->getSourceDataset<DatasetImpl>()->getRawDataName();
            const bool hasSameRaw = candidateDataset->getRawDataName() == toLevelData->getRawDataName();
            const bool isProxy    = candidateDataset->isProxy() && candidateDataset->getProxyMembers().contains(toLevelData);

            if (isDerived || hasSameRaw || isProxy) {
                events().notifyDatasetDataSelectionChanged(candidateDataset);
            }
        }

    }

}

void PyramidImage::scan() const
{
    
    if (!std::filesystem::exists(_tiffFilePath.toStdString())) {
        fmt::println("PyramidImage::scan: _tiffFilePath does not exist: {}", _tiffFilePath.toStdString());
        return;
    }
    if (!std::filesystem::exists(_jsonFilePath.toStdString())) {
        fmt::println("PyramidImage::scan: _jsonFilePath does not exist: {}", _jsonFilePath.toStdString());
        return;
    }

    const auto pyramidData = getRawData<PyramidImageData>();

    const bool scanSuccess = pyramidData->scan(_tiffFilePath, _jsonFilePath);

    if (!scanSuccess) {
        fmt::println("PyramidImage::scan: Could not scan the image");
        _infoAction->getReadLevelAction().setDisabled(true);
        return;
    }

    const auto& imagePyramid = pyramidData->getPyramid();

    const auto& series = imagePyramid.series();
    const auto numLevels = series.pyramid.size();

    QStringList resolutions;
    resolutions.reserve(numLevels);
    for (size_t levelID = 0; levelID < numLevels; levelID++) {
        const auto& level = series.pyramid[levelID];
        resolutions.emplace_back(QString("%1 x %2 (level %3)").arg(level.width).arg(level.height).arg(levelID));
    }

    _infoAction->getTiffFilePathAction().setString(_tiffFilePath);
    _infoAction->getJsonFilePathAction().setString(_jsonFilePath);
    _infoAction->getNumberOfLevelsAction().setString(QString::number(numLevels));
    _infoAction->getNumberOfChannelsAction().setString(QString::number(series.channels));
    _infoAction->getResolutionsAction().setOptions(resolutions);
    _infoAction->getResolutionsAction().setCurrentIndex(static_cast<int>(numLevels - 1));
    _infoAction->getReadLevelAction().setEnabled(true);

}

void PyramidImage::read_level()
{
    selectNone();

    // 0. Get data
    const auto selectedLevel = _infoAction->getResolutionsAction().getCurrentIndex();
    const auto pyramidData = getRawData<PyramidImageData>();
    const auto& imagePyramid = pyramidData->getPyramid();
    const auto [lvlWidth, lvlHeight, lvlNumChannels, lvlChannelNames, lvlDataChannelMajor] = imagePyramid.read_level(selectedLevel);
    const double scaleFactor = imagePyramid.series().scaleFactor(selectedLevel);

    fmt::println("PyramidImage::read_level {}, width {}, height", selectedLevel, lvlWidth, lvlHeight);

    // Convert channel names
	std::vector<QString> channelNames;
    channelNames.reserve(lvlNumChannels);
    for (auto& s : lvlChannelNames) channelNames.emplace_back(QString::fromStdString(s));

    // Reshape to HWC (height-width-channel) from CHW (channel-height-width)
    // TODO: consider load data in HWC (height-width-channel) format instead of CHW (channel-height-width)
    auto channelMajorToPixelMajor = [](
        const std::vector<float>& input, const uint32_t channels, const uint32_t height, const uint32_t width)
        -> std::vector<float>
        {
            const size_t numPixels = static_cast<size_t>(height) * width;
            std::vector<float> output(numPixels * channels);

#pragma omp parallel for
            for (int64_t row = 0; row < static_cast<int64_t>(height); ++row) {
            	for (uint32_t col = 0; col < width; ++col) {
                    const size_t pixelOut = static_cast<size_t>(row) * width + col;
                    const size_t pixelIn = static_cast<size_t>(height - 1 - static_cast<size_t>(row)) * width + col;

                    for (uint32_t c = 0; c < channels; ++c) {
                        output[pixelOut * channels + c] = input[numPixels * c + pixelIn];
                    }
                }
            }

            return output;
        };

    // 1. Publish Image data //
    auto pointsDatasetLevel = mv::data().createDataset<Points>(QStringLiteral("Points"), QString("Level (%1)").arg(selectedLevel), this);
    _levelDatasets.insert({ pointsDatasetLevel.getDatasetId(), { pointsDatasetLevel, selectedLevel } });

    pointsDatasetLevel->setData(channelMajorToPixelMajor(lvlDataChannelMajor, lvlNumChannels, lvlHeight, lvlWidth), lvlNumChannels);
    pointsDatasetLevel->setDimensionNames(channelNames);

    events().notifyDatasetDataChanged(pointsDatasetLevel);
    events().notifyDatasetDataDimensionsChanged(pointsDatasetLevel);

    auto imagesDataset = mv::data().createDataset<Images>(QStringLiteral("Images"), QStringLiteral("Images"), Dataset<DatasetImpl>(*pointsDatasetLevel));

    imagesDataset->setText(QString("Images (%1x%2)").arg(QString::number(lvlWidth)).arg(QString::number(lvlHeight)));
    imagesDataset->setType(ImageData::Type::Stack);
    imagesDataset->setNumberOfImages(lvlNumChannels);
    imagesDataset->setImageSize(QSize(static_cast<int>(lvlWidth), static_cast<int>(lvlHeight)));
    imagesDataset->setNumberOfComponentsPerPixel(1);

    events().notifyDatasetDataChanged(imagesDataset);
    imagesDataset->getDataHierarchyItem().select();

    // 2. Publish Mask data //
    auto publicMaskData = [&](std::vector<uint32_t>& maskIDs, const std::vector<uint32_t>& pixel_counts, const std::vector<std::string>& polygonNames,
        const QString& dataPrefix, const std::vector<std::array<uint8_t, 3>>* colors = nullptr)
    {
        auto pointsDatasetLevelSelection = pointsDatasetLevel->getSelection<Points>();
        auto& selectionIDs = pointsDatasetLevelSelection->indices;
        selectionIDs.clear();
        selectionIDs.swap(maskIDs);
        auto maskedPointData = mv::data().createSubsetFromSelection(pointsDatasetLevelSelection, pointsDatasetLevel, dataPrefix + QStringLiteral(" data"), pointsDatasetLevel, true, true);
        selectionIDs.swap(maskIDs);

        auto clustersData = mv::data().createDataset<Clusters>(QStringLiteral("Cluster"), dataPrefix + QStringLiteral(" clusters"), pointsDatasetLevel);

        assert(polygonNames.size() == pixel_counts.size());
        assert(!colors || colors->size() == pixel_counts.size());

        uint32_t idsBegin = 0;
        for (size_t roiID = 0; roiID < pixel_counts.size(); roiID++)
        {
            if (pixel_counts[roiID] == 0)
                continue;

            const uint32_t idsEnd = idsBegin + pixel_counts[roiID];
            const std::vector<uint32_t> clusterIDs(maskIDs.cbegin() + idsBegin, maskIDs.cbegin() + idsEnd);
            idsBegin = idsEnd;

            assert(clusterIDs.size() == pixel_counts[roiID]);

            Cluster cluster(
                QString::fromStdString(polygonNames[roiID]),
                {},
                clusterIDs
            );

            if (colors) {
                const auto& color = colors->at(roiID);
                cluster.setColor({ color[0], color[1], color[2] });
            }
            clustersData->addCluster(cluster);

        }

        if (!colors)
        {
            Cluster::colorizeClusters(
                clustersData->getClusters(),
                42);
        }

        events().notifyDatasetDataChanged(clustersData);

    };

    if (pyramidData->getPolygons().has_roi())
    {
        fmt::println("Transform ROI mask");
        auto [maskIDs_roi, pixel_counts_roi] = pyramidData->getPolygons().getMaskRoi(scaleFactor);
        publicMaskData(maskIDs_roi, pixel_counts_roi, pyramidData->getPolygons().names_roi(), "ROI", &pyramidData->getPolygons().colors_roi());
    }
    if (pyramidData->getPolygons().has_tissue())
    {
        fmt::println("Transform TISSUE mask");
        auto [maskIDs_tissue, pixel_counts_tissue] = pyramidData->getPolygons().getMaskTissue(scaleFactor);
        publicMaskData(maskIDs_tissue, pixel_counts_tissue, pyramidData->getPolygons().names_tissue(), "TISSUE", &pyramidData->getPolygons().colors_tissue());
    }
    if (pyramidData->getPolygons().has_cell())
    {
        fmt::println("Transform CELL mask");
        auto [maskIDs_cell, pixel_counts_cell] = pyramidData->getPolygons().getMaskCell(scaleFactor);
        publicMaskData(maskIDs_cell, pixel_counts_cell, pyramidData->getPolygons().names_cell(), "CELL");
    }
    if (pyramidData->getPolygons().has_nucleus())
    {
        fmt::println("Transform NUCLEUS mask");
        auto [maskIDs_nucleus, pixel_counts_nucleus] = pyramidData->getPolygons().getMaskNucleus(scaleFactor);
        publicMaskData(maskIDs_nucleus, pixel_counts_nucleus, pyramidData->getPolygons().names_cell(), "NUCLEUS");
    }

}

std::vector<std::uint32_t>& PyramidImage::getSelectionIndices()
{
    return selection;
}

void PyramidImage::setSelectionIndices(const std::vector<std::uint32_t>& selectionIndices)
{
    selection = selectionIndices;
}

void PyramidImage::setSelectionIndices(std::vector<std::uint32_t>&& selectionIndices)
{
    selection = std::move(selectionIndices);
}

bool PyramidImage::canSelect() const
{
    if (_levelDatasets.empty()) return false;

    const mv::Dataset<Points> firstData = _levelDatasets.begin()->second.first;
    return firstData->getNumPoints() > 0;
}

bool PyramidImage::canSelectAll() const
{
    if (_levelDatasets.empty()) return false;

    return _levelDatasets.begin()->second.first->canSelectAll();
}

bool PyramidImage::canSelectNone() const
{
    if (_levelDatasets.empty()) return false;

    return _levelDatasets.begin()->second.first->canSelectNone();
}

bool PyramidImage::canSelectInvert() const
{
    if (_levelDatasets.empty()) return false;

    return _levelDatasets.begin()->second.first->canSelectInvert();
}

void PyramidImage::selectAll()
{
    if (!canSelectAll()) return;

    _levelDatasets.begin()->second.first->selectAll();
}

void PyramidImage::selectNone()
{
    if (!canSelectNone()) return;
	
	_selectionCount = 0;
    _levelDatasets.begin()->second.first->selectNone();
}

void PyramidImage::selectInvert()
{
    if (!canSelectInvert()) return;

    _levelDatasets.begin()->second.first->selectInvert();
}

void PyramidImage::fromVariantMap(const QVariantMap& variantMap)
{
    DatasetImpl::fromVariantMap(variantMap);

    // In init() check if these two files are available
    // Yes: scan the image
    // No: disable the read button
    _tiffFilePath = variantMap[SID_tiffFilePath].toString();
    _jsonFilePath = variantMap[SID_jsonFilePath].toString();

    if (std::filesystem::exists(_tiffFilePath.toStdString()) && std::filesystem::exists(_jsonFilePath.toStdString())) {
        scan();
    }
    else {
        _infoAction->getReadLevelAction().setDisabled(true);
        _infoAction->getTiffFilePathAction().setString(QStringLiteral(u"File not found"));
        _infoAction->getJsonFilePathAction().setString(QStringLiteral(u"File not found"));
    }

    {
        for (const auto [dataID, selectionCount] : variantMap[SID_levelDatasets].toMap().asKeyValueRange()) {
            _levelDatasets[dataID] = std::make_pair(
                mv::data().getDataset(dataID),
                static_cast<uint32_t>(selectionCount.toUInt())
            );
        }
    }

    events().notifyDatasetDataChanged(this);
}

QVariantMap PyramidImage::toVariantMap() const
{
    auto variantMap = DatasetImpl::toVariantMap();

    variantMap[SID_tiffFilePath] = QVariant::fromValue(_tiffFilePath);
    variantMap[SID_jsonFilePath] = QVariant::fromValue(_jsonFilePath);

    // do not save _selectionCounter but reset all counters
    // to zero when loading _levelDatasets 
	{
		QVariantMap levelDatasetsMap;
    	for (const auto& [dataID, dataLevelPair] : _levelDatasets) {
            levelDatasetsMap.insert(dataID, dataLevelPair.second);
    	}
    	variantMap[SID_levelDatasets] = levelDatasetsMap;
	}

    return variantMap;
}

// =============================================================================
// Factory
// =============================================================================

PyramidImageDataFactory::PyramidImageDataFactory()
{
    RawDataFactory::setIconByName(QStringLiteral(u"database"));
}

plugin::RawData* PyramidImageDataFactory::produce()
{
    return new PyramidImageData(this);
}
