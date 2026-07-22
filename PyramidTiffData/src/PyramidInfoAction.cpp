// SPDX-License-Identifier: LGPL-3.0-or-later 
// A corresponding LICENSE file is located in the root directory of this source tree 
// Copyright (C) 2023 BioVault (Biomedical Visual Analytics Unit LUMC - TU Delft) 

#include "PyramidInfoAction.h"

using namespace mv::gui;

PyramidInfoAction::PyramidInfoAction(QObject* parent, PyramidImage& pyramidImage) :
    GroupAction(parent, "Group", true),
    _pyramidImage(&pyramidImage),
    _tiffFilePathAction(this, "Tiff file"),
    _jsonFilePathAction(this, "JSON file"),
    _numberOfLevelsAction(this, "Levels"),
    _numberOfChannelsAction(this, "Channels"),
    _resolutionsAction(this, "Resolutions"),
    _loadRoisAction(this, "Load Rois"),
    _loadTissuesAction(this, "Load Tissues"),
    _loadCellsAction(this, "Load Cells"),
    _loadNucleiAction(this, "Load Nuclei"),
    _readLevelAction(this, "Read level")
{
    setText("Info");

    auto readOnlyWidget = [this](WidgetAction* action, QWidget* widget) -> void {
        if (auto lineWidget = widget->findChild<QLineEdit*>("LineEdit"))
            lineWidget->setReadOnly(true);
        };

    GroupAction::addAction(&_tiffFilePathAction, -1, readOnlyWidget);
    GroupAction::addAction(&_jsonFilePathAction, -1, readOnlyWidget);
    GroupAction::addAction(&_numberOfLevelsAction, -1, readOnlyWidget);
    GroupAction::addAction(&_numberOfChannelsAction, -1, readOnlyWidget);
    GroupAction::addAction(&_resolutionsAction);
    GroupAction::addAction(&_loadRoisAction);
    GroupAction::addAction(&_loadTissuesAction);
    GroupAction::addAction(&_loadCellsAction);
    GroupAction::addAction(&_loadNucleiAction);
    GroupAction::addAction(&_readLevelAction);
}

void PyramidInfoAction::fromVariantMap(const QVariantMap& variantMap)
{
    GroupAction::fromVariantMap(variantMap);

    _tiffFilePathAction.fromVariantMap(variantMap);
    _jsonFilePathAction.fromVariantMap(variantMap);
    _numberOfLevelsAction.fromVariantMap(variantMap);
    _numberOfChannelsAction.fromVariantMap(variantMap);
    _resolutionsAction.fromVariantMap(variantMap);
    _readLevelAction.fromVariantMap(variantMap);
    _loadRoisAction.fromVariantMap(variantMap);
    _loadTissuesAction.fromVariantMap(variantMap);
    _loadCellsAction.fromVariantMap(variantMap);
    _loadNucleiAction.fromVariantMap(variantMap);
}

QVariantMap PyramidInfoAction::toVariantMap() const
{
    auto variantMap = GroupAction::toVariantMap();

    _tiffFilePathAction.insertIntoVariantMap(variantMap);
    _jsonFilePathAction.insertIntoVariantMap(variantMap);
    _numberOfLevelsAction.insertIntoVariantMap(variantMap);
    _numberOfChannelsAction.insertIntoVariantMap(variantMap);
    _resolutionsAction.insertIntoVariantMap(variantMap);
    _readLevelAction.insertIntoVariantMap(variantMap);
    _loadRoisAction.insertIntoVariantMap(variantMap);
    _loadTissuesAction.insertIntoVariantMap(variantMap);
    _loadCellsAction.insertIntoVariantMap(variantMap);
    _loadNucleiAction.insertIntoVariantMap(variantMap);

    return variantMap;
}
