/*
  ==============================================================================

    SettingsComponent.cpp

This file is part of MIDI2LR. Copyright 2015-2016 by Rory Jaffe.

MIDI2LR is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

MIDI2LR is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
MIDI2LR.  If not, see <http://www.gnu.org/licenses/>.
  ==============================================================================
*/

#include "SettingsComponent.h"
#include <utility>
#include "../JuceLibraryCode/JuceHeader.h"

namespace {
  constexpr auto SettingsLeft = 20;
  constexpr auto SettingsWidth = 400;
  constexpr auto SettingsHeight = 300;
}

SettingsComponent::SettingsComponent(): ResizableLayout{this} {}

SettingsComponent::~SettingsComponent() {}

void SettingsComponent::Init(std::weak_ptr<SettingsManager>&& settings_manager) {
    //copy the pointer
  settings_manager_ = std::move(settings_manager);

  // for layouts to work you must start at some size
  // place controls in a location that is initially correct.
  setSize(SettingsWidth, SettingsHeight);

  if (const auto ptr = settings_manager_.lock()) {
    pickup_group_.setText("Pick up");
    pickup_group_.setBounds(0, 0, SettingsWidth, 100);
    addToLayout(&pickup_group_, anchorMidLeft, anchorMidRight);
    addAndMakeVisible(pickup_group_);

    pickup_label_.setFont(juce::Font{12.f, juce::Font::bold});
    pickup_label_.setText("Disabling the pickup mode may be better for touchscreen interfaces and may solve issues with LR not picking up fast fader/knob movements", NotificationType::dontSendNotification);
    pickup_label_.setBounds(SettingsLeft, 15, SettingsWidth - 2 * SettingsLeft, 50);
    addToLayout(&pickup_label_, anchorMidLeft, anchorMidRight);
    pickup_label_.setEditable(false);
    pickup_label_.setColour(juce::Label::textColourId, juce::Colours::darkgrey);
    addAndMakeVisible(pickup_label_);

    pickup_enabled_.addListener(this);
    pickup_enabled_.setToggleState(ptr->getPickupEnabled(), juce::NotificationType::dontSendNotification);
    pickup_enabled_.setBounds(SettingsLeft, 60, SettingsWidth - 2 * SettingsLeft, 32);
    addToLayout(&pickup_enabled_, anchorMidLeft, anchorMidRight);
    addAndMakeVisible(pickup_enabled_);

    // ---------------------------- profile section -----------------------------------
    profile_group_.setText("Profile");
    profile_group_.setBounds(0, 100, SettingsWidth, 100);
    addToLayout(&profile_group_, anchorMidLeft, anchorMidRight);
    addAndMakeVisible(profile_group_);

    profile_location_button_.addListener(this);
    profile_location_button_.setBounds(SettingsLeft, 120, SettingsWidth - 2 * SettingsLeft, 25);
    addToLayout(&profile_location_button_, anchorMidLeft, anchorMidRight);
    addAndMakeVisible(profile_location_button_);

    profile_location_label_.setEditable(false);
    profile_location_label_.setBounds(SettingsLeft, 145, SettingsWidth - 2 * SettingsLeft, 30);
    addToLayout(&profile_location_label_, anchorMidLeft, anchorMidRight);
    profile_location_label_.setColour(juce::Label::textColourId, juce::Colours::darkgrey);
    addAndMakeVisible(profile_location_label_);
    profile_location_label_.setText(ptr->getProfileDirectory(), juce::NotificationType::dontSendNotification);

    ////// ----------------------- auto hide section ------------------------------------
    autohide_group_.setText("Auto hide");
    autohide_group_.setBounds(0, 200, SettingsWidth, 100);
    addToLayout(&autohide_group_, anchorMidLeft, anchorMidRight);
    addAndMakeVisible(autohide_group_);

    autohide_explain_label_.setFont(juce::Font{12.f, juce::Font::bold});
    autohide_explain_label_.setText("Autohide the plugin window in x seconds, select 0 for disabling autohide", juce::NotificationType::dontSendNotification);
    autohide_explain_label_.setBounds(SettingsLeft, 215, SettingsWidth - 2 * SettingsLeft, 50);
    addToLayout(&autohide_explain_label_, anchorMidLeft, anchorMidRight);
    autohide_explain_label_.setEditable(false);
    autohide_explain_label_.setFont(juce::Font{12.f, juce::Font::bold});
    autohide_explain_label_.setColour(juce::Label::textColourId, juce::Colours::darkgrey);
    addAndMakeVisible(autohide_explain_label_);

    autohide_setting_.setBounds(SettingsLeft, 245, SettingsWidth - 2 * SettingsLeft, 50);
    autohide_setting_.setRange(0, 10, 1);
    autohide_setting_.setValue(ptr->getAutoHideTime(), juce::NotificationType::dontSendNotification);

    addToLayout(&autohide_setting_, anchorMidLeft, anchorMidRight);
    //add this as the lister for the data
    autohide_setting_.addListener(this);
    addAndMakeVisible(autohide_setting_);
    // turn it on
    activateLayout();
  }
}

void SettingsComponent::paint(Graphics& g) {
  g.fillAll(Colours::white);   // clear the background
}

void SettingsComponent::buttonClicked(Button* button) {
  if (button == &pickup_enabled_) {
    if (const auto ptr = settings_manager_.lock())
      ptr->setPickupEnabled(pickup_enabled_.getToggleState());
  }
  else if (button == &profile_location_button_) {
    FileBrowserComponent browser{
      FileBrowserComponent::canSelectDirectories | FileBrowserComponent::openMode,
        File::getCurrentWorkingDirectory(), nullptr, nullptr};

    FileChooserDialogBox dialog_box{"Select Profile Folder",
        "Select a folder containing MIDI2LR Profiles",
        browser, true, Colours::lightgrey};

    if (dialog_box.show()) {
      const auto profile_location = browser.getSelectedFile(0).getFullPathName();
      if (const auto ptr = settings_manager_.lock()) {
        ptr->setProfileDirectory(profile_location);
      }
      profile_location_label_.setText(profile_location,
        NotificationType::dontSendNotification);
    }
  }
}

void SettingsComponent::sliderValueChanged(Slider* slider) {
  if (slider && &autohide_setting_ == slider)
    if (const auto ptr = settings_manager_.lock())
      ptr->setAutoHideTime(static_cast<int>(autohide_setting_.getValue()));
}