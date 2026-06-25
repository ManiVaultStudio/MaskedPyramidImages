// SPDX-License-Identifier: LGPL-3.0-or-later 
// A corresponding LICENSE file is located in the root directory of this source tree 
// Copyright (C) 2023 BioVault (Biomedical Visual Analytics Unit LUMC - TU Delft) 

#pragma once

#include "PyramidTiffData.h"

#include "actions/OptionAction.h"
#include "actions/StringAction.h"
#include "actions/TriggerAction.h"

#include "Dataset.h"

/**
 * Action class for displaying basic pyramid tiff images info
 *
 */
class PyramidInfoAction : public mv::gui::GroupAction
{
    Q_OBJECT

public:

    /**
     * Constructor
     * @param parent Pointer to parent object
     * @param pyramidImage Reference to images dataset
     */
    PyramidInfoAction(QObject* parent, PyramidImage& pyramidImage);

    mv::gui::StringAction& getTiffFilePathAction() { return _tiffFilePathAction; }
    mv::gui::StringAction& getJsonFilePathAction() { return _jsonFilePathAction; }
    mv::gui::StringAction& getNumberOfLevelsAction() { return _numberOfLevelsAction; }
    mv::gui::StringAction& getNumberOfChannelsAction() { return _numberOfChannelsAction; }
    mv::gui::OptionAction& getResolutionsAction() { return _resolutionsAction; }
    mv::gui::TriggerAction& getReadLevelAction() { return _readLevelAction; }

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

protected:
    mv::Dataset<PyramidImage>       _pyramidImage;                      /** Points dataset reference */
    mv::gui::StringAction           _tiffFilePathAction;                /** Tiff file path action */
    mv::gui::StringAction           _jsonFilePathAction;                /** JSON file path action */
    mv::gui::StringAction           _numberOfLevelsAction;              /** Number of pyramid levels action */
    mv::gui::StringAction           _numberOfChannelsAction;            /** Number of channels action */
    mv::gui::OptionAction           _resolutionsAction;                 /** List of pyramid resolutions action */
    mv::gui::TriggerAction          _readLevelAction;                   /** List of pyramid resolutions action */
};
