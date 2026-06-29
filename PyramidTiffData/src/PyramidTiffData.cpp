#include "PyramidTiffData.h"

#include "PyramidInfoAction.h"

#include <event/Event.h>

#include <ImageData/Images.h>
#include <PointData/PointData.h>
#include <ClusterData/ClusterData.h>

#include <fmt/base.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
//#include <ranges>
#include <string>

//#if !defined(__clang__) && (defined(__GNUC__) || defined(_MSC_VER))
//#if defined(__GNUC__)  // both TBB and Qt define emit keyword
//#undef emit
//#endif
//#include <execution>
//#if defined(__GNUC__) // both TBB and Qt define emit keyword
//#define emit
//#endif
//#ifdef NDEBUG
//#define MV_PYRAMID_PARALLEL_EXECUTION std::execution::par,
//#else
//#define MV_PYRAMID_PARALLEL_EXECUTION std::execution::seq,
//#endif
//#else // __clang__
//#define MV_PYRAMID_PARALLEL_EXECUTION
//#endif

Q_PLUGIN_METADATA(IID "studio.manivault.PyramidImageData")

using namespace mv;

// =============================================================================
// Helper
// =============================================================================

namespace
{
    void sortAndUnique(std::vector<uint32_t>& v)
    {
        if (v.size() <= 1)
            return;

        //std::sort(MV_PYRAMID_PARALLEL_EXECUTION
        //    v.begin(),
        //    v.end());
        //auto last = std::unique(MV_PYRAMID_PARALLEL_EXECUTION
        //    v.begin(),
        //    v.end());

        //std::sort(v.begin(), v.end());
        auto last = std::unique(v.begin(), v.end());
    	
    	v.erase(last, v.end());
    }

    std::vector<uint32_t> convertSelectionToDownscaled(
        const std::vector<uint32_t>& selectedIndices,
        const uint32_t originalWidth, const uint32_t originalHeight,
        const uint32_t newWidth, const uint32_t newHeight)
    {
        const double scaleFactor = static_cast<double>(newWidth) / static_cast<double>(originalWidth);
        assert(scaleFactor <= 1.0);

        const uint32_t newSize = newWidth * newHeight;
        std::vector<uint8_t> bitmap(newSize, 0);

        uint32_t prevIdx = 0;
        uint32_t x = 0;
        uint32_t y = 0;

        for (const uint32_t idx : selectedIndices) {
            const uint32_t delta = idx - prevIdx;
            x += delta;
            while (x >= originalWidth) {
                x -= originalWidth; ++y;
            }
            prevIdx = idx;

            const uint32_t newX = static_cast<uint32_t>(static_cast<double>(x) * scaleFactor);
            const uint32_t newY = static_cast<uint32_t>(static_cast<double>(y) * scaleFactor);

            bitmap[newY * newWidth + newX] = 1;
        }

        std::vector<uint32_t> result;
        result.reserve(newSize); // upper bound
        for (uint32_t i = 0; i < newSize; ++i)
            if (bitmap[i]) result.push_back(i);
        result.shrink_to_fit();
        return result;
    }

    std::vector<uint32_t> convertSelectionToUpscaled(
        const std::vector<uint32_t>& selectedIndices,
        const uint32_t originalWidth, const uint32_t originalHeight,
        const uint32_t newWidth, const uint32_t newHeight)
    {
        if (selectedIndices.empty())
            return {};

        const double scaleFactor = static_cast<double>(newWidth) / static_cast<double>(originalWidth);
        assert(scaleFactor >= 1.0);

        const uint32_t newSize = newWidth * newHeight;
        std::vector<uint8_t> bitmap(newSize, 0);

        uint32_t prevIdx = 0;
        uint32_t x = 0, y = 0;

        for (const uint32_t idx : selectedIndices) {
            const uint32_t delta = idx - prevIdx;
            x += delta;
            while (x >= originalWidth) { x -= originalWidth; ++y; }
            prevIdx = idx;

            // Expand each pixel into a d by d block
            // The inverse of floor(newX / scaleFactor) == x
            // is the range: [x * scaleFactor, (x+1) * scaleFactor)
            const uint32_t newXStart = static_cast<uint32_t>(std::floor(x * scaleFactor));
            const uint32_t newXEnd = std::min(static_cast<uint32_t>(std::floor((x + 1) * scaleFactor)), newWidth);
            const uint32_t newYStart = static_cast<uint32_t>(std::floor(y * scaleFactor));
            const uint32_t newYEnd = std::min(static_cast<uint32_t>(std::floor((y + 1) * scaleFactor)), newHeight);

            for (uint32_t ny = newYStart; ny < newYEnd; ++ny)
                std::memset(&bitmap[ny * newWidth + newXStart], 1, newXEnd - newXStart);
        }

        std::vector<uint32_t> result;
        result.reserve(newSize); // upper bound
        for (uint32_t i = 0; i < newSize; ++i)
            if (bitmap[i]) result.push_back(i);
        result.shrink_to_fit();
        return result;
    }
}

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

//const QString PyramidImage::SID_levelDatasets = QStringLiteral("LevelDatasets");
//const QString PyramidImage::SID_tiffFilePath = QStringLiteral("TiffFilePath");
//const QString PyramidImage::SID_jsonFilePath = QStringLiteral("JsonFilePath");

PyramidImage::PyramidImage(const QString& dataName, const bool mayUnderive, const QString& guid) :
    DatasetImpl(dataName, mayUnderive, guid)
{
}

void PyramidImage::init()
{
    DatasetImpl::init();

	setIconByName("square-caret-up");

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
                        
            if (const auto itSelection = _selectionCounter.find(datasetID); itSelection != _selectionCounter.end())
                _selectionCounter.erase(itSelection);

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

void PyramidImage::selectionMapping(const mv::Dataset<>& input)
{
    // Get level info
	const mv::Dataset<Points> selectionInputPoints = input;
    const auto fromLevelID = selectionInputPoints.getDatasetId();

    if (_selectionCounter.at(fromLevelID) > 0) {

        // Have all datasets been handled? Then reset the counter
    	uint32_t selectionCount = 0;
        for (const auto& [key, count] : _selectionCounter)
            selectionCount += count;

        if (selectionCount == _selectionCounter.size()) {
            for (auto& [key, count] : _selectionCounter)
                count = 0;
        }

        return;
    }

    _selectionCounter.at(fromLevelID) = 1;

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
    mv::Dataset<Points> selectionIDs = selectionInputPoints->getSelection();

    sortAndUnique(selectionIDs->indices);

    auto baseIndices = (fromLevel == 0) ?
        selectionIDs->indices :
        convertSelectionToUpscaled(selectionIDs->indices, 
            fromLevelWidth, fromLevelHeigh, 
            baseWidth, baseHeigh);

    sortAndUnique(baseIndices);

    // Map from base to all other levels
    for (const auto& [toLevelID, toLevelPair] : _levelDatasets)
    {
        auto& [toLevelData, toLevel] = toLevelPair;
        if (toLevelID == fromLevelID) continue;

        const uint32_t toLevelWidth = levelInfos[toLevel].width;
        const uint32_t toLevelHeigh = levelInfos[toLevel].height;

        // Map from base to level
        if (toLevel != fromLevel) {
        	auto levelIndices = convertSelectionToDownscaled(baseIndices, 
                baseWidth, baseHeigh,
                toLevelWidth, toLevelHeigh);
            toLevelData->getSelection<Points>()->indices.swap(levelIndices);
        }
        else {
            toLevelData->getSelection<Points>()->indices = selectionIDs->indices;
        }

        _selectionCounter.at(toLevelID) = 1;
    	events().notifyDatasetDataSelectionChanged(toLevelData);
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
	
    std::vector<float> imageDataValues = channelMajorToPixelMajor(lvlDataChannelMajor, lvlNumChannels, lvlHeight, lvlWidth);

    auto [maskIDs, pixel_counts] = pyramidData->getPolygons().downscaleMask(scaleFactor);

#pragma omp parallel for
    for (int64_t id = 0; id < static_cast<int64_t>(maskIDs.size()); ++id) {
        uint32_t v = maskIDs[id];
        uint32_t row = v / lvlWidth;
        uint32_t col = v % lvlWidth;
        maskIDs[id] = (lvlHeight - 1 - row) * lvlWidth + col;
    }

	const auto& polygonNames = pyramidData->getPolygons().names();
    assert(polygonNames.size() == pixel_counts.size());

    // 1. Publish Image data //
    auto pointsDataset = mv::data().createDataset<Points>(QStringLiteral("Points"), QString("Level (%1)").arg(selectedLevel), this);
    _levelDatasets.insert({ pointsDataset.getDatasetId(), { pointsDataset, selectedLevel } });
    _selectionCounter.insert({ pointsDataset.getDatasetId(), 0 });

    pointsDataset->setData(std::move(imageDataValues), lvlNumChannels);
    pointsDataset->setDimensionNames(channelNames);

    events().notifyDatasetDataChanged(pointsDataset);
    events().notifyDatasetDataDimensionsChanged(pointsDataset);

    auto imagesDataset = mv::data().createDataset<Images>(QStringLiteral("Images"), QStringLiteral("Images"), Dataset<DatasetImpl>(*pointsDataset));

    imagesDataset->setText(QString("Images (%1x%2)").arg(QString::number(lvlWidth)).arg(QString::number(lvlHeight)));
    imagesDataset->setType(ImageData::Type::Stack);
    imagesDataset->setNumberOfImages(lvlNumChannels);
    imagesDataset->setImageSize(QSize(static_cast<int>(lvlWidth), static_cast<int>(lvlHeight)));
    imagesDataset->setNumberOfComponentsPerPixel(1);

    events().notifyDatasetDataChanged(imagesDataset);
    imagesDataset->getDataHierarchyItem().select();

    // 2. Publish Mask data //
    auto pointsDatasetSelection = pointsDataset->getSelection<Points>();
    auto& selectionIDs = pointsDatasetSelection->indices;
    selectionIDs.clear();
    selectionIDs.swap(maskIDs);
    auto maskDataset = mv::data().createSubsetFromSelection(pointsDatasetSelection, pointsDataset, QStringLiteral("Masked data"), pointsDataset, true, true);
    selectionIDs.swap(maskIDs);

    auto clusterDataset = mv::data().createDataset<Clusters>(QStringLiteral("Cluster"), QStringLiteral("Masked clusters"), pointsDataset);

    uint32_t idsBegin = 0;
    for (size_t roiID = 0; roiID < pixel_counts.size(); roiID++)
    {
        const uint32_t idsEnd = idsBegin + pixel_counts[roiID];
        const std::vector<uint32_t> clusterIDs(maskIDs.cbegin() + idsBegin, maskIDs.cbegin() + idsEnd);
        idsBegin = idsEnd;

        assert(clusterIDs.size() == pixel_counts[roiID]);

        Cluster cluster(
            QString::fromStdString(polygonNames[roiID]),
			{},
            clusterIDs
        );

        clusterDataset->addCluster(cluster);
    }

    Cluster::colorizeClusters(
        clusterDataset->getClusters(),
        42);

    events().notifyDatasetDataChanged(clusterDataset);
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
    if (_levelDatasets.empty())
	    return false;

    const mv::Dataset<Points> firstData = _levelDatasets.begin()->second.first;
    return firstData->getNumPoints() > 0;
}

bool PyramidImage::canSelectAll() const
{
    if (_levelDatasets.empty())
        return false;

    const mv::Dataset<Points> firstData = _levelDatasets.begin()->second.first;
    return firstData->canSelectAll();
}

bool PyramidImage::canSelectNone() const
{
    if (_levelDatasets.empty())
        return false;

    const mv::Dataset<Points> firstData = _levelDatasets.begin()->second.first;
    return firstData->canSelectNone();
}

bool PyramidImage::canSelectInvert() const
{
    if (_levelDatasets.empty())
        return false;

    const mv::Dataset<Points> firstData = _levelDatasets.begin()->second.first;
    return firstData->canSelectInvert();
}

void PyramidImage::selectAll()
{
    if (!canSelectAll())
        return;

    mv::Dataset<Points> firstData = _levelDatasets.begin()->second.first;
    firstData->selectAll();
}

void PyramidImage::selectNone()
{
    if (!canSelectNone())
        return;

    mv::Dataset<Points> firstData = _levelDatasets.begin()->second.first;
    firstData->selectNone();
}

void PyramidImage::selectInvert()
{
    if (!canSelectInvert())
        return;

    mv::Dataset<Points> firstData = _levelDatasets.begin()->second.first;
    firstData->selectInvert();
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
        _infoAction->getTiffFilePathAction().setString(QStringLiteral("File not found"));
        _infoAction->getJsonFilePathAction().setString(QStringLiteral("File not found"));
    }

    {
        for (const auto [dataID, selectionCount] : variantMap[SID_levelDatasets].toMap().asKeyValueRange()) {
            _selectionCounter[dataID] = 0;
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
    RawDataFactory::setIconByName("database");
}

plugin::RawData* PyramidImageDataFactory::produce()
{
    return new PyramidImageData(this);
}
