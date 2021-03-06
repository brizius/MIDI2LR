/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2015 - ROLI Ltd.

   Permission is granted to use this software under the terms of either:
   a) the GPL v2 (or any later version)
   b) the Affero GPL v3

   Details of these licenses can be found at: www.gnu.org/licenses

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

   ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.juce.com for more information.

  ==============================================================================
*/

namespace PopupMenuSettings
{
    const int scrollZone = 24;
    const int borderSize = 2;
    const int dismissCommandId = 0x6287345f;

    static bool menuWasHiddenBecauseOfAppChange = false;
}

//==============================================================================
struct PopupMenu::HelperClasses
{

class MouseSourceState;
class MenuWindow;

static bool canBeTriggered (const PopupMenu::Item& item) noexcept        { return item.isEnabled && item.itemID != 0 && ! item.isSectionHeader; }
static bool hasActiveSubMenu (const PopupMenu::Item& item) noexcept      { return item.isEnabled && item.subMenu != nullptr && item.subMenu->items.size() > 0; }
static const Colour* getColour (const PopupMenu::Item& item) noexcept    { return item.colour != Colour (0x00000000) ? &item.colour : nullptr; }
static bool hasSubMenu (const PopupMenu::Item& item) noexcept            { return item.subMenu != nullptr && (item.itemID == 0 || item.subMenu->getNumItems() > 0); }

//==============================================================================
struct HeaderItemComponent  : public PopupMenu::CustomComponent
{
    HeaderItemComponent (const String& name)  : PopupMenu::CustomComponent (false)
    {
        setName (name);
    }

    void paint (Graphics& g) override
    {
        getLookAndFeel().drawPopupMenuSectionHeader (g, getLocalBounds(), getName());
    }

    void getIdealSize (int& idealWidth, int& idealHeight) override
    {
        getLookAndFeel().getIdealPopupMenuItemSize (getName(), false, -1, idealWidth, idealHeight);
        idealHeight += idealHeight / 2;
        idealWidth += idealWidth / 4;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderItemComponent)
};

//==============================================================================
struct ItemComponent  : public Component
{
    ItemComponent (const PopupMenu::Item& i, int standardItemHeight, MenuWindow& parent)
      : item (i),
        customComp (i.customComponent),
        isHighlighted (false)
    {
        if (item.isSectionHeader)
            customComp = new HeaderItemComponent (item.text);

        addAndMakeVisible (customComp);

        parent.addAndMakeVisible (this);

        updateShortcutKeyDescription();

        int itemW = 80;
        int itemH = 16;
        getIdealSize (itemW, itemH, standardItemHeight);
        setSize (itemW, jlimit (2, 600, itemH));

        addMouseListener (&parent, false);
    }

    ~ItemComponent()
    {
        removeChildComponent (customComp);
    }

    void getIdealSize (int& idealWidth, int& idealHeight, const int standardItemHeight)
    {
        if (customComp != nullptr)
            customComp->getIdealSize (idealWidth, idealHeight);
        else
            getLookAndFeel().getIdealPopupMenuItemSize (getTextForMeasurement(),
                                                        item.isSeparator,
                                                        standardItemHeight,
                                                        idealWidth, idealHeight);
    }

    void paint (Graphics& g) override
    {
        if (customComp == nullptr)
            getLookAndFeel().drawPopupMenuItem (g, getLocalBounds(),
                                                item.isSeparator,
                                                item.isEnabled,
                                                isHighlighted,
                                                item.isTicked,
                                                hasSubMenu (item),
                                                item.text,
                                                item.shortcutKeyDescription,
                                                item.image,
                                                getColour (item));
    }

    void resized() override
    {
        if (Component* const child = getChildComponent (0))
            child->setBounds (getLocalBounds().reduced (2, 0));
    }

    void setHighlighted (bool shouldBeHighlighted)
    {
        shouldBeHighlighted = shouldBeHighlighted && item.isEnabled;

        if (isHighlighted != shouldBeHighlighted)
        {
            isHighlighted = shouldBeHighlighted;

            if (customComp != nullptr)
                customComp->setHighlighted (shouldBeHighlighted);

            repaint();
        }
    }

    PopupMenu::Item item;

private:
    // NB: we use a copy of the one from the item info in case we're using our own section comp
    ReferenceCountedObjectPtr<CustomComponent> customComp;
    bool isHighlighted;

    void updateShortcutKeyDescription()
    {
        if (item.commandManager != nullptr
             && item.itemID != 0
             && item.shortcutKeyDescription.isEmpty())
        {
            String shortcutKey;
            const Array<KeyPress> keyPresses (item.commandManager->getKeyMappings()
                                                 ->getKeyPressesAssignedToCommand (item.itemID));

            for (int i = 0; i < keyPresses.size(); ++i)
            {
                const String key (keyPresses.getReference(i).getTextDescriptionWithIcons());

                if (shortcutKey.isNotEmpty())
                    shortcutKey << ", ";

                if (key.length() == 1 && key[0] < 128)
                    shortcutKey << "shortcut: '" << key << '\'';
                else
                    shortcutKey << key;
            }

            item.shortcutKeyDescription = shortcutKey.trim();
        }
    }

    String getTextForMeasurement() const
    {
        return item.shortcutKeyDescription.isNotEmpty() ? item.text + "   " + item.shortcutKeyDescription
                                                        : item.text;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ItemComponent)
};

//==============================================================================
class MenuWindow  : public Component
{
public:
    MenuWindow (const PopupMenu& menu, MenuWindow* const parentWindow,
                const Options& opts,
                const bool alignToRectangle,
                const bool shouldDismissOnMouseUp,
                ApplicationCommandManager** const manager)
       : Component ("menu"),
         parent (parentWindow),
         options (opts),
         managerOfChosenCommand (manager),
         componentAttachedTo (options.targetComponent),
         parentComponent (nullptr),
         hasBeenOver (false),
         needsToScroll (false),
         dismissOnMouseUp (shouldDismissOnMouseUp),
         hideOnExit (false),
         disableMouseMoves (false),
         hasAnyJuceCompHadFocus (false),
         numColumns (0),
         contentHeight (0),
         childYOffset (0),
         windowCreationTime (Time::getMillisecondCounter()),
         lastFocusedTime (windowCreationTime),
         timeEnteredCurrentChildComp (windowCreationTime)
    {
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
        setAlwaysOnTop (true);

        setLookAndFeel (parent != nullptr ? &(parent->getLookAndFeel())
                                          : menu.lookAndFeel.get());

        parentComponent = getLookAndFeel().getParentComponentForMenuOptions (options);

        setOpaque (getLookAndFeel().findColour (PopupMenu::backgroundColourId).isOpaque()
                     || ! Desktop::canUseSemiTransparentWindows());

        for (int i = 0; i < menu.items.size(); ++i)
        {
            PopupMenu::Item* const item = menu.items.getUnchecked(i);

            if (i < menu.items.size() - 1 || ! item->isSeparator)
                items.add (new ItemComponent (*item, options.standardHeight, *this));
        }

        calculateWindowPos (options.targetArea, alignToRectangle);
        setTopLeftPosition (windowPos.getPosition());
        updateYPositions();

        if (options.visibleItemID != 0)
        {
            const Point<int> targetPosition =
                (parentComponent != nullptr ? parentComponent->getLocalPoint (nullptr, options.targetArea.getTopLeft())
                                            : options.targetArea.getTopLeft());

            const int y = targetPosition.getY() - windowPos.getY();
            ensureItemIsVisible (options.visibleItemID,
                                 isPositiveAndBelow (y, windowPos.getHeight()) ? y : -1);
        }

        resizeToBestWindowPos();

        if (parentComponent != nullptr)
        {
            parentComponent->addChildComponent (this);
        }
        else
        {
            addToDesktop (ComponentPeer::windowIsTemporary
                          | ComponentPeer::windowIgnoresKeyPresses
                          | getLookAndFeel().getMenuWindowFlags());

            getActiveWindows().add (this);
            Desktop::getInstance().addGlobalMouseListener (this);
        }
    }

    ~MenuWindow()
    {
        getActiveWindows().removeFirstMatchingValue (this);
        Desktop::getInstance().removeGlobalMouseListener (this);
        activeSubMenu = nullptr;
        items.clear();
    }

    //==============================================================================
    void paint (Graphics& g) override
    {
        if (isOpaque())
            g.fillAll (Colours::white);

        getLookAndFeel().drawPopupMenuBackground (g, getWidth(), getHeight());
    }

    void paintOverChildren (Graphics& g) override
    {
        LookAndFeel& lf = getLookAndFeel();

        if (parentComponent != nullptr)
            lf.drawResizableFrame (g, getWidth(), getHeight(), BorderSize<int> (PopupMenuSettings::borderSize));

        if (canScroll())
        {
            if (isTopScrollZoneActive())
                lf.drawPopupMenuUpDownArrow (g, getWidth(), PopupMenuSettings::scrollZone, true);

            if (isBottomScrollZoneActive())
            {
                g.setOrigin (0, getHeight() - PopupMenuSettings::scrollZone);
                lf.drawPopupMenuUpDownArrow (g, getWidth(), PopupMenuSettings::scrollZone, false);
            }
        }
    }

    //==============================================================================
    // hide this and all sub-comps
    void hide (const PopupMenu::Item* const item, const bool makeInvisible)
    {
        if (isVisible())
        {
            WeakReference<Component> deletionChecker (this);

            activeSubMenu = nullptr;
            currentChild = nullptr;

            if (item != nullptr
                 && item->commandManager != nullptr
                 && item->itemID != 0)
            {
                *managerOfChosenCommand = item->commandManager;
            }

            exitModalState (item != nullptr ? item->itemID : 0);

            if (makeInvisible && (deletionChecker != nullptr))
                setVisible (false);
        }
    }

    void dismissMenu (const PopupMenu::Item* const item)
    {
        if (parent != nullptr)
        {
            parent->dismissMenu (item);
        }
        else
        {
            if (item != nullptr)
            {
                // need a copy of this on the stack as the one passed in will get deleted during this call
                const PopupMenu::Item mi (*item);
                hide (&mi, false);
            }
            else
            {
                hide (nullptr, false);
            }
        }
    }

    //==============================================================================
    bool keyPressed (const KeyPress& key) override
    {
        if (key.isKeyCode (KeyPress::downKey))
        {
            selectNextItem (1);
        }
        else if (key.isKeyCode (KeyPress::upKey))
        {
            selectNextItem (-1);
        }
        else if (key.isKeyCode (KeyPress::leftKey))
        {
            if (parent != nullptr)
            {
                Component::SafePointer<MenuWindow> parentWindow (parent);
                ItemComponent* currentChildOfParent = parentWindow->currentChild;

                hide (nullptr, true);

                if (parentWindow != nullptr)
                    parentWindow->setCurrentlyHighlightedChild (currentChildOfParent);

                disableTimerUntilMouseMoves();
            }
            else if (componentAttachedTo != nullptr)
            {
                componentAttachedTo->keyPressed (key);
            }
        }
        else if (key.isKeyCode (KeyPress::rightKey))
        {
            disableTimerUntilMouseMoves();

            if (showSubMenuFor (currentChild))
            {
                if (isSubMenuVisible())
                    activeSubMenu->selectNextItem (1);
            }
            else if (componentAttachedTo != nullptr)
            {
                componentAttachedTo->keyPressed (key);
            }
        }
        else if (key.isKeyCode (KeyPress::returnKey))
        {
            triggerCurrentlyHighlightedItem();
        }
        else if (key.isKeyCode (KeyPress::escapeKey))
        {
            dismissMenu (nullptr);
        }
        else
        {
            return false;
        }

        return true;
    }

    void inputAttemptWhenModal() override
    {
        WeakReference<Component> deletionChecker (this);

        for (int i = mouseSourceStates.size(); --i >= 0;)
        {
            mouseSourceStates.getUnchecked(i)->timerCallback();

            if (deletionChecker == nullptr)
                return;
        }

        if (! isOverAnyMenu())
        {
            if (componentAttachedTo != nullptr)
            {
                // we want to dismiss the menu, but if we do it synchronously, then
                // the mouse-click will be allowed to pass through. That's good, except
                // when the user clicks on the button that originally popped the menu up,
                // as they'll expect the menu to go away, and in fact it'll just
                // come back. So only dismiss synchronously if they're not on the original
                // comp that we're attached to.
                const Point<int> mousePos (componentAttachedTo->getMouseXYRelative());

                if (componentAttachedTo->reallyContains (mousePos, true))
                {
                    postCommandMessage (PopupMenuSettings::dismissCommandId); // dismiss asynchrounously
                    return;
                }
            }

            dismissMenu (nullptr);
        }
    }

    void handleCommandMessage (int commandId) override
    {
        Component::handleCommandMessage (commandId);

        if (commandId == PopupMenuSettings::dismissCommandId)
            dismissMenu (nullptr);
    }

    //==============================================================================
    void mouseMove  (const MouseEvent& e) override    { handleMouseEvent (e); }
    void mouseDown  (const MouseEvent& e) override    { handleMouseEvent (e); }
    void mouseDrag  (const MouseEvent& e) override    { handleMouseEvent (e); }
    void mouseUp    (const MouseEvent& e) override    { handleMouseEvent (e); }

    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& wheel) override
    {
        alterChildYPos (roundToInt (-10.0f * wheel.deltaY * PopupMenuSettings::scrollZone));
    }

    void handleMouseEvent (const MouseEvent& e)
    {
        getMouseState (e.source).handleMouseEvent (e);
    }

    bool windowIsStillValid()
    {
        if (! isVisible())
            return false;

        if (componentAttachedTo != options.targetComponent)
        {
            dismissMenu (nullptr);
            return false;
        }

        if (MenuWindow* currentlyModalWindow = dynamic_cast<MenuWindow*> (Component::getCurrentlyModalComponent()))
            if (! treeContains (currentlyModalWindow))
                return false;

        return true;
    }

    static Array<MenuWindow*>& getActiveWindows()
    {
        static Array<MenuWindow*> activeMenuWindows;
        return activeMenuWindows;
    }

    MouseSourceState& getMouseState (MouseInputSource source)
    {
        for (int i = mouseSourceStates.size(); --i >= 0;)
        {
            MouseSourceState& ms = *mouseSourceStates.getUnchecked(i);
            if (ms.source == source)
                return ms;
        }

        MouseSourceState* ms = new MouseSourceState (*this, source);
        mouseSourceStates.add (ms);
        return *ms;
    }

    //==============================================================================
    bool isOverAnyMenu() const
    {
        return parent != nullptr ? parent->isOverAnyMenu()
                                 : isOverChildren();
    }

    bool isOverChildren() const
    {
        return isVisible()
                && (isAnyMouseOver() || (activeSubMenu != nullptr && activeSubMenu->isOverChildren()));
    }

    bool isAnyMouseOver() const
    {
        for (int i = 0; i < mouseSourceStates.size(); ++i)
            if (mouseSourceStates.getUnchecked(i)->isOver())
                return true;

        return false;
    }

    bool treeContains (const MenuWindow* const window) const noexcept
    {
        const MenuWindow* mw = this;

        while (mw->parent != nullptr)
            mw = mw->parent;

        while (mw != nullptr)
        {
            if (mw == window)
                return true;

            mw = mw->activeSubMenu;
        }

        return false;
    }

    bool doesAnyJuceCompHaveFocus()
    {
        bool anyFocused = Process::isForegroundProcess();

        if (anyFocused && Component::getCurrentlyFocusedComponent() == nullptr)
        {
            // because no component at all may have focus, our test here will
            // only be triggered when something has focus and then loses it.
            anyFocused = ! hasAnyJuceCompHadFocus;

            for (int i = ComponentPeer::getNumPeers(); --i >= 0;)
            {
                if (ComponentPeer::getPeer (i)->isFocused())
                {
                    anyFocused = true;
                    hasAnyJuceCompHadFocus = true;
                    break;
                }
            }
        }

        return anyFocused;
    }

    //==============================================================================
    Rectangle<int> getParentArea (Point<int> targetPoint)
    {
        Rectangle<int> parentArea (Desktop::getInstance().getDisplays()
                                   .getDisplayContaining (targetPoint)
                                  #if JUCE_MAC
                                   .userArea);
                                  #else
                                   .totalArea); // on windows, don't stop the menu overlapping the taskbar
                                  #endif

        if (parentComponent == nullptr)
            return parentArea;

        return parentComponent->getLocalArea (nullptr,
                                              parentComponent->getScreenBounds()
                                                    .reduced (PopupMenuSettings::borderSize)
                                                    .getIntersection (parentArea));
    }

    void calculateWindowPos (Rectangle<int> target, const bool alignToRectangle)
    {
        const Rectangle<int> parentArea = getParentArea (target.getCentre());

        if (parentComponent != nullptr)
            target = parentComponent->getLocalArea (nullptr, target).getIntersection (parentArea);

        const int maxMenuHeight = parentArea.getHeight() - 24;

        int x, y, widthToUse, heightToUse;
        layoutMenuItems (parentArea.getWidth() - 24, maxMenuHeight, widthToUse, heightToUse);

        if (alignToRectangle)
        {
            x = target.getX();

            const int spaceUnder = parentArea.getHeight() - (target.getBottom() - parentArea.getY());
            const int spaceOver = target.getY() - parentArea.getY();

            if (heightToUse < spaceUnder - 30 || spaceUnder >= spaceOver)
                y = target.getBottom();
            else
                y = target.getY() - heightToUse;
        }
        else
        {
            bool tendTowardsRight = target.getCentreX() < parentArea.getCentreX();

            if (parent != nullptr)
            {
                if (parent->parent != nullptr)
                {
                    const bool parentGoingRight = (parent->getX() + parent->getWidth() / 2
                                                    > parent->parent->getX() + parent->parent->getWidth() / 2);

                    if (parentGoingRight && target.getRight() + widthToUse < parentArea.getRight() - 4)
                        tendTowardsRight = true;
                    else if ((! parentGoingRight) && target.getX() > widthToUse + 4)
                        tendTowardsRight = false;
                }
                else if (target.getRight() + widthToUse < parentArea.getRight() - 32)
                {
                    tendTowardsRight = true;
                }
            }

            const int biggestSpace = jmax (parentArea.getRight() - target.getRight(),
                                           target.getX() - parentArea.getX()) - 32;

            if (biggestSpace < widthToUse)
            {
                layoutMenuItems (biggestSpace + target.getWidth() / 3, maxMenuHeight, widthToUse, heightToUse);

                if (numColumns > 1)
                    layoutMenuItems (biggestSpace - 4, maxMenuHeight, widthToUse, heightToUse);

                tendTowardsRight = (parentArea.getRight() - target.getRight()) >= (target.getX() - parentArea.getX());
            }

            if (tendTowardsRight)
                x = jmin (parentArea.getRight() - widthToUse - 4, target.getRight());
            else
                x = jmax (parentArea.getX() + 4, target.getX() - widthToUse);

            y = target.getY();
            if (target.getCentreY() > parentArea.getCentreY())
                y = jmax (parentArea.getY(), target.getBottom() - heightToUse);
        }

        x = jmax (parentArea.getX() + 1, jmin (parentArea.getRight() - (widthToUse + 6), x));
        y = jmax (parentArea.getY() + 1, jmin (parentArea.getBottom() - (heightToUse + 6), y));

        windowPos.setBounds (x, y, widthToUse, heightToUse);

        // sets this flag if it's big enough to obscure any of its parent menus
        hideOnExit = parent != nullptr
                      && parent->windowPos.intersects (windowPos.expanded (-4, -4));
    }

    void layoutMenuItems (const int maxMenuW, const int maxMenuH, int& width, int& height)
    {
        numColumns = 0;
        contentHeight = 0;
        int totalW;

        const int maximumNumColumns = options.maxColumns > 0 ? options.maxColumns : 7;

        do
        {
            ++numColumns;
            totalW = workOutBestSize (maxMenuW);

            if (totalW > maxMenuW)
            {
                numColumns = jmax (1, numColumns - 1);
                workOutBestSize (maxMenuW); // to update col widths
                break;
            }
            else if (totalW > maxMenuW / 2 || contentHeight < maxMenuH)
            {
                break;
            }

        } while (numColumns < maximumNumColumns);

        const int actualH = jmin (contentHeight, maxMenuH);

        needsToScroll = contentHeight > actualH;

        width = updateYPositions();
        height = actualH + PopupMenuSettings::borderSize * 2;
    }

    int workOutBestSize (const int maxMenuW)
    {
        int totalW = 0;
        contentHeight = 0;
        int childNum = 0;

        for (int col = 0; col < numColumns; ++col)
        {
            int colW = options.standardHeight, colH = 0;

            const int numChildren = jmin (items.size() - childNum,
                                          (items.size() + numColumns - 1) / numColumns);

            for (int i = numChildren; --i >= 0;)
            {
                colW = jmax (colW, items.getUnchecked (childNum + i)->getWidth());
                colH += items.getUnchecked (childNum + i)->getHeight();
            }

            colW = jmin (maxMenuW / jmax (1, numColumns - 2), colW + PopupMenuSettings::borderSize * 2);

            columnWidths.set (col, colW);
            totalW += colW;
            contentHeight = jmax (contentHeight, colH);

            childNum += numChildren;
        }

        // width must never be larger than the screen
        const int minWidth = jmin (maxMenuW, options.minWidth);

        if (totalW < minWidth)
        {
            totalW = minWidth;

            for (int col = 0; col < numColumns; ++col)
                columnWidths.set (0, totalW / numColumns);
        }

        return totalW;
    }

    void ensureItemIsVisible (const int itemID, int wantedY)
    {
        jassert (itemID != 0);

        for (int i = items.size(); --i >= 0;)
        {
            if (ItemComponent* const m = items.getUnchecked(i))
            {
                if (m->item.itemID == itemID
                     && windowPos.getHeight() > PopupMenuSettings::scrollZone * 4)
                {
                    const int currentY = m->getY();

                    if (wantedY > 0 || currentY < 0 || m->getBottom() > windowPos.getHeight())
                    {
                        if (wantedY < 0)
                            wantedY = jlimit (PopupMenuSettings::scrollZone,
                                              jmax (PopupMenuSettings::scrollZone,
                                                    windowPos.getHeight() - (PopupMenuSettings::scrollZone + m->getHeight())),
                                              currentY);

                        const Rectangle<int> parantArea = getParentArea (windowPos.getPosition());

                        int deltaY = wantedY - currentY;

                        windowPos.setSize (jmin (windowPos.getWidth(), parantArea.getWidth()),
                                           jmin (windowPos.getHeight(), parantArea.getHeight()));

                        const int newY = jlimit (parantArea.getY(),
                                                 parantArea.getBottom() - windowPos.getHeight(),
                                                 windowPos.getY() + deltaY);

                        deltaY -= newY - windowPos.getY();

                        childYOffset -= deltaY;
                        windowPos.setPosition (windowPos.getX(), newY);

                        updateYPositions();
                    }

                    break;
                }
            }
        }
    }

    void resizeToBestWindowPos()
    {
        Rectangle<int> r (windowPos);

        if (childYOffset < 0)
        {
            r = r.withTop (r.getY() - childYOffset);
        }
        else if (childYOffset > 0)
        {
            const int spaceAtBottom = r.getHeight() - (contentHeight - childYOffset);

            if (spaceAtBottom > 0)
                r.setSize (r.getWidth(), r.getHeight() - spaceAtBottom);
        }

        setBounds (r);
        updateYPositions();
    }

    void alterChildYPos (const int delta)
    {
        if (canScroll())
        {
            childYOffset += delta;

            if (delta < 0)
                childYOffset = jmax (childYOffset, 0);
            else if (delta > 0)
                childYOffset = jmin (childYOffset,
                                     contentHeight - windowPos.getHeight() + PopupMenuSettings::borderSize);

            updateYPositions();
        }
        else
        {
            childYOffset = 0;
        }

        resizeToBestWindowPos();
        repaint();
    }

    int updateYPositions()
    {
        int x = 0;
        int childNum = 0;

        for (int col = 0; col < numColumns; ++col)
        {
            const int numChildren = jmin (items.size() - childNum,
                                          (items.size() + numColumns - 1) / numColumns);

            const int colW = columnWidths [col];

            int y = PopupMenuSettings::borderSize - (childYOffset + (getY() - windowPos.getY()));

            for (int i = 0; i < numChildren; ++i)
            {
                Component* const c = items.getUnchecked (childNum + i);
                c->setBounds (x, y, colW, c->getHeight());
                y += c->getHeight();
            }

            x += colW;
            childNum += numChildren;
        }

        return x;
    }

    void setCurrentlyHighlightedChild (ItemComponent* const child)
    {
        if (currentChild != nullptr)
            currentChild->setHighlighted (false);

        currentChild = child;

        if (currentChild != nullptr)
        {
            currentChild->setHighlighted (true);
            timeEnteredCurrentChildComp = Time::getApproximateMillisecondCounter();
        }
    }

    bool isSubMenuVisible() const noexcept          { return activeSubMenu != nullptr && activeSubMenu->isVisible(); }

    bool showSubMenuFor (ItemComponent* const childComp)
    {
        activeSubMenu = nullptr;

        if (childComp != nullptr
             && hasActiveSubMenu (childComp->item))
        {
            activeSubMenu = new HelperClasses::MenuWindow (*(childComp->item.subMenu), this,
                                                           options.withTargetScreenArea (childComp->getScreenBounds())
                                                                  .withMinimumWidth (0)
                                                                  .withTargetComponent (nullptr),
                                                           false, dismissOnMouseUp, managerOfChosenCommand);

            activeSubMenu->setVisible (true); // (must be called before enterModalState on Windows to avoid DropShadower confusion)
            activeSubMenu->enterModalState (false);
            activeSubMenu->toFront (false);
            return true;
        }

        return false;
    }

    void triggerCurrentlyHighlightedItem()
    {
        if (currentChild != nullptr
             && canBeTriggered (currentChild->item)
             && (currentChild->item.customComponent == nullptr
                  || currentChild->item.customComponent->isTriggeredAutomatically()))
        {
            dismissMenu (&currentChild->item);
        }
    }

    void selectNextItem (const int delta)
    {
        disableTimerUntilMouseMoves();

        int start = jmax (0, items.indexOf (currentChild));

        for (int i = items.size(); --i >= 0;)
        {
            start += delta;

            if (ItemComponent* mic = items.getUnchecked ((start + items.size()) % items.size()))
            {
                if (canBeTriggered (mic->item) || hasActiveSubMenu (mic->item))
                {
                    setCurrentlyHighlightedChild (mic);
                    break;
                }
            }
        }
    }

    void disableTimerUntilMouseMoves()
    {
        disableMouseMoves = true;

        if (parent != nullptr)
            parent->disableTimerUntilMouseMoves();
    }

    bool canScroll() const noexcept                 { return childYOffset != 0 || needsToScroll; }
    bool isTopScrollZoneActive() const noexcept     { return canScroll() && childYOffset > 0; }
    bool isBottomScrollZoneActive() const noexcept  { return canScroll() && childYOffset < contentHeight - windowPos.getHeight(); }

    //==============================================================================
    MenuWindow* parent;
    const Options options;
    OwnedArray<ItemComponent> items;
    ApplicationCommandManager** managerOfChosenCommand;
    WeakReference<Component> componentAttachedTo;
    Component* parentComponent;
    Rectangle<int> windowPos;
    bool hasBeenOver, needsToScroll;
    bool dismissOnMouseUp, hideOnExit, disableMouseMoves, hasAnyJuceCompHadFocus;
    int numColumns, contentHeight, childYOffset;
    Component::SafePointer<ItemComponent> currentChild;
    ScopedPointer<MenuWindow> activeSubMenu;
    Array<int> columnWidths;
    uint32 windowCreationTime, lastFocusedTime, timeEnteredCurrentChildComp;
    OwnedArray<MouseSourceState> mouseSourceStates;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MenuWindow)
};

//==============================================================================
class MouseSourceState  : private Timer
{
public:
    MouseSourceState (MenuWindow& w, MouseInputSource s)
        : window (w), source (s), scrollAcceleration (1.0),
          lastScrollTime (Time::getMillisecondCounter()),
          lastMouseMoveTime (0), isDown (false)
    {
    }

    void handleMouseEvent (const MouseEvent& e)
    {
        if (! window.windowIsStillValid())
            return;

        startTimerHz (20);
        handleMousePosition (e.getScreenPosition());
    }

    void timerCallback() override
    {
        if (window.windowIsStillValid())
            handleMousePosition (source.getScreenPosition().roundToInt());
    }

    bool isOver() const
    {
        return window.reallyContains (window.getLocalPoint (nullptr, source.getScreenPosition()).roundToInt(), true);
    }

    MenuWindow& window;
    MouseInputSource source;

private:
    Point<int> lastMousePos;
    double scrollAcceleration;
    uint32 lastScrollTime, lastMouseMoveTime;
    bool isDown;

    void handleMousePosition (Point<int> globalMousePos)
    {
        const Point<int> localMousePos (window.getLocalPoint (nullptr, globalMousePos));

        const uint32 timeNow = Time::getMillisecondCounter();

        if (timeNow > window.timeEnteredCurrentChildComp + 100
             && window.reallyContains (localMousePos, true)
             && window.currentChild != nullptr
             && ! (window.disableMouseMoves || window.isSubMenuVisible()))
        {
            window.showSubMenuFor (window.currentChild);
        }

        highlightItemUnderMouse (globalMousePos, localMousePos, timeNow);

        const bool overScrollArea = scrollIfNecessary (localMousePos, timeNow);
        const bool isOverAny = window.isOverAnyMenu();

        if (window.hideOnExit && window.hasBeenOver && ! isOverAny)
            window.hide (nullptr, true);
        else
            checkButtonState (localMousePos, timeNow, isDown, overScrollArea, isOverAny);
    }

    void checkButtonState (Point<int> localMousePos, const uint32 timeNow,
                           const bool wasDown, const bool overScrollArea, const bool isOverAny)
    {
        isDown = window.hasBeenOver
                    && (ModifierKeys::getCurrentModifiers().isAnyMouseButtonDown()
                         || ModifierKeys::getCurrentModifiersRealtime().isAnyMouseButtonDown());

        if (! window.doesAnyJuceCompHaveFocus())
        {
            if (timeNow > window.lastFocusedTime + 10)
            {
                PopupMenuSettings::menuWasHiddenBecauseOfAppChange = true;
                window.dismissMenu (nullptr);
                // Note: this object may have been deleted by the previous call..
            }
        }
        else if (wasDown && timeNow > window.windowCreationTime + 250
                   && ! (isDown || overScrollArea))
        {
            if (window.reallyContains (localMousePos, true))
                window.triggerCurrentlyHighlightedItem();
            else if ((window.hasBeenOver || ! window.dismissOnMouseUp) && ! isOverAny)
                window.dismissMenu (nullptr);

            // Note: this object may have been deleted by the previous call..
        }
        else
        {
            window.lastFocusedTime = timeNow;
        }
    }

    void highlightItemUnderMouse (Point<int> globalMousePos, Point<int> localMousePos, const uint32 timeNow)
    {
        if (globalMousePos != lastMousePos || timeNow > lastMouseMoveTime + 350)
        {
            const bool isMouseOver = window.reallyContains (localMousePos, true);

            if (isMouseOver)
                window.hasBeenOver = true;

            if (lastMousePos.getDistanceFrom (globalMousePos) > 2)
            {
                lastMouseMoveTime = timeNow;

                if (window.disableMouseMoves && isMouseOver)
                    window.disableMouseMoves = false;
            }

            if (window.disableMouseMoves || (window.activeSubMenu != nullptr && window.activeSubMenu->isOverChildren()))
                return;

            const bool isMovingTowardsMenu = isMouseOver && globalMousePos != lastMousePos
                                                && isMovingTowardsSubmenu (globalMousePos);

            lastMousePos = globalMousePos;

            if (! isMovingTowardsMenu)
            {
                Component* c = window.getComponentAt (localMousePos);
                if (c == &window)
                    c = nullptr;

                ItemComponent* itemUnderMouse = dynamic_cast<ItemComponent*> (c);

                if (itemUnderMouse == nullptr && c != nullptr)
                    itemUnderMouse = c->findParentComponentOfClass<ItemComponent>();

                if (itemUnderMouse != window.currentChild
                      && (isMouseOver || (window.activeSubMenu == nullptr) || ! window.activeSubMenu->isVisible()))
                {
                    if (isMouseOver && (c != nullptr) && (window.activeSubMenu != nullptr))
                        window.activeSubMenu->hide (nullptr, true);

                    if (! isMouseOver)
                        itemUnderMouse = nullptr;

                    window.setCurrentlyHighlightedChild (itemUnderMouse);
                }
            }
        }
    }

    bool isMovingTowardsSubmenu (Point<int> newGlobalPos) const
    {
        if (window.activeSubMenu == nullptr)
            return false;

        // try to intelligently guess whether the user is moving the mouse towards a currently-open
        // submenu. To do this, look at whether the mouse stays inside a triangular region that
        // extends from the last mouse pos to the submenu's rectangle..

        const Rectangle<int> itemScreenBounds (window.activeSubMenu->getScreenBounds());
        float subX = (float) itemScreenBounds.getX();

        Point<int> oldGlobalPos (lastMousePos);

        if (itemScreenBounds.getX() > window.getX())
        {
            oldGlobalPos -= Point<int> (2, 0);  // to enlarge the triangle a bit, in case the mouse only moves a couple of pixels
        }
        else
        {
            oldGlobalPos += Point<int> (2, 0);
            subX += itemScreenBounds.getWidth();
        }

        Path areaTowardsSubMenu;
        areaTowardsSubMenu.addTriangle ((float) oldGlobalPos.x, (float) oldGlobalPos.y,
                                        subX, (float) itemScreenBounds.getY(),
                                        subX, (float) itemScreenBounds.getBottom());

        return areaTowardsSubMenu.contains (newGlobalPos.toFloat());
    }

    bool scrollIfNecessary (Point<int> localMousePos, const uint32 timeNow)
    {
        if (window.canScroll()
             && isPositiveAndBelow (localMousePos.x, window.getWidth())
             && (isPositiveAndBelow (localMousePos.y, window.getHeight()) || source.isDragging()))
        {
            if (window.isTopScrollZoneActive() && localMousePos.y < PopupMenuSettings::scrollZone)
                return scroll (timeNow, -1);

            if (window.isBottomScrollZoneActive() && localMousePos.y > window.getHeight() - PopupMenuSettings::scrollZone)
                return scroll (timeNow, 1);
        }

        scrollAcceleration = 1.0;
        return false;
    }

    bool scroll (const uint32 timeNow, const int direction)
    {
        if (timeNow > lastScrollTime + 20)
        {
            scrollAcceleration = jmin (4.0, scrollAcceleration * 1.04);
            int amount = 0;

            for (int i = 0; i < window.items.size() && amount == 0; ++i)
                amount = ((int) scrollAcceleration) * window.items.getUnchecked(i)->getHeight();

            window.alterChildYPos (amount * direction);
            lastScrollTime = timeNow;
        }

        return true;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MouseSourceState)
};

//==============================================================================
struct NormalComponentWrapper : public PopupMenu::CustomComponent
{
    NormalComponentWrapper (Component* const comp, const int w, const int h,
                            const bool triggerMenuItemAutomaticallyWhenClicked)
        : PopupMenu::CustomComponent (triggerMenuItemAutomaticallyWhenClicked),
          width (w), height (h)
    {
        addAndMakeVisible (comp);
    }

    void getIdealSize (int& idealWidth, int& idealHeight) override
    {
        idealWidth = width;
        idealHeight = height;
    }

    void resized() override
    {
        if (Component* const child = getChildComponent(0))
            child->setBounds (getLocalBounds());
    }

    const int width, height;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NormalComponentWrapper)
};

};

//==============================================================================
PopupMenu::PopupMenu()
{
}

PopupMenu::PopupMenu (const PopupMenu& other)
    : lookAndFeel (other.lookAndFeel)
{
    items.addCopiesOf (other.items);
}

PopupMenu& PopupMenu::operator= (const PopupMenu& other)
{
    if (this != &other)
    {
        lookAndFeel = other.lookAndFeel;

        clear();
        items.addCopiesOf (other.items);
    }

    return *this;
}

#if JUCE_COMPILER_SUPPORTS_MOVE_SEMANTICS
PopupMenu::PopupMenu (PopupMenu&& other) noexcept
    : lookAndFeel (other.lookAndFeel)
{
    items.swapWith (other.items);
}

PopupMenu& PopupMenu::operator= (PopupMenu&& other) noexcept
{
    jassert (this != &other); // hopefully the compiler should make this situation impossible!

    items.swapWith (other.items);
    lookAndFeel = other.lookAndFeel;
    return *this;
}
#endif

PopupMenu::~PopupMenu()
{
}

void PopupMenu::clear()
{
    items.clear();
}

//==============================================================================
PopupMenu::Item::Item() noexcept
  : itemID (0),
    commandManager (nullptr),
    colour (0x00000000),
    isEnabled (true),
    isTicked (false),
    isSeparator (false),
    isSectionHeader (false)
{
}

PopupMenu::Item::Item (const Item& other)
  : text (other.text),
    itemID (other.itemID),
    subMenu (createCopyIfNotNull (other.subMenu.get())),
    image (other.image != nullptr ? other.image->createCopy() : nullptr),
    customComponent (other.customComponent),
    commandManager (other.commandManager),
    shortcutKeyDescription (other.shortcutKeyDescription),
    colour (other.colour),
    isEnabled (other.isEnabled),
    isTicked (other.isTicked),
    isSeparator (other.isSeparator),
    isSectionHeader (other.isSectionHeader)
{
}

PopupMenu::Item& PopupMenu::Item::operator= (const Item& other)
{
    text = other.text;
    itemID = other.itemID;
    subMenu = createCopyIfNotNull (other.subMenu.get());
    image = (other.image != nullptr ? other.image->createCopy() : nullptr);
    customComponent = other.customComponent;
    commandManager = other.commandManager;
    shortcutKeyDescription = other.shortcutKeyDescription;
    colour = other.colour;
    isEnabled = other.isEnabled;
    isTicked = other.isTicked;
    isSeparator = other.isSeparator;
    isSectionHeader = other.isSectionHeader;
    return *this;
}

void PopupMenu::addItem (const Item& newItem)
{
    // An ID of 0 is used as a return value to indicate that the user
    // didn't pick anything, so you shouldn't use it as the ID for an item..
    jassert (newItem.itemID != 0
              || newItem.isSeparator || newItem.isSectionHeader
              || newItem.subMenu != nullptr);

    items.add (new Item (newItem));
}

void PopupMenu::addItem (int itemResultID, const String& itemText, bool isActive, bool isTicked)
{
    Item i;
    i.text = itemText;
    i.itemID = itemResultID;
    i.isEnabled = isActive;
    i.isTicked = isTicked;
    addItem (i);
}

static Drawable* createDrawableFromImage (const Image& im)
{
    if (im.isValid())
    {
        DrawableImage* d = new DrawableImage();
        d->setImage (im);
        return d;
    }

    return nullptr;
}

void PopupMenu::addItem (int itemResultID, const String& itemText, bool isActive, bool isTicked, const Image& iconToUse)
{
    addItem (itemResultID, itemText, isActive, isTicked, createDrawableFromImage (iconToUse));
}

void PopupMenu::addItem (int itemResultID, const String& itemText, bool isActive, bool isTicked, Drawable* iconToUse)
{
    Item i;
    i.text = itemText;
    i.itemID = itemResultID;
    i.isEnabled = isActive;
    i.isTicked = isTicked;
    i.image = iconToUse;
    addItem (i);
}

void PopupMenu::addCommandItem (ApplicationCommandManager* commandManager,
                                const CommandID commandID,
                                const String& displayName,
                                Drawable* iconToUse)
{
    jassert (commandManager != nullptr && commandID != 0);

    if (const ApplicationCommandInfo* const registeredInfo = commandManager->getCommandForID (commandID))
    {
        ApplicationCommandInfo info (*registeredInfo);
        ApplicationCommandTarget* const target = commandManager->getTargetForCommand (commandID, info);

        Item i;
        i.text = displayName.isNotEmpty() ? displayName : info.shortName;
        i.itemID = (int) commandID;
        i.commandManager = commandManager;
        i.isEnabled = target != nullptr && (info.flags & ApplicationCommandInfo::isDisabled) == 0;
        i.isTicked = (info.flags & ApplicationCommandInfo::isTicked) != 0;
        i.image = iconToUse;
        addItem (i);
    }
}

void PopupMenu::addColouredItem (int itemResultID, const String& itemText, Colour itemTextColour,
                                 bool isActive, bool isTicked, Drawable* iconToUse)
{
    Item i;
    i.text = itemText;
    i.itemID = itemResultID;
    i.colour = itemTextColour;
    i.isEnabled = isActive;
    i.isTicked = isTicked;
    i.image = iconToUse;
    addItem (i);
}

void PopupMenu::addColouredItem (int itemResultID, const String& itemText, Colour itemTextColour,
                                 bool isActive, bool isTicked, const Image& iconToUse)
{
    Item i;
    i.text = itemText;
    i.itemID = itemResultID;
    i.colour = itemTextColour;
    i.isEnabled = isActive;
    i.isTicked = isTicked;
    i.image = createDrawableFromImage (iconToUse);
    addItem (i);
}

void PopupMenu::addCustomItem (int itemResultID, CustomComponent* cc, const PopupMenu* subMenu)
{
    Item i;
    i.itemID = itemResultID;
    i.customComponent = cc;
    i.subMenu = createCopyIfNotNull (subMenu);
    addItem (i);
}

void PopupMenu::addCustomItem (int itemResultID, Component* customComponent, int idealWidth, int idealHeight,
                               bool triggerMenuItemAutomaticallyWhenClicked, const PopupMenu* subMenu)
{
    addCustomItem (itemResultID,
                   new HelperClasses::NormalComponentWrapper (customComponent, idealWidth, idealHeight,
                                                              triggerMenuItemAutomaticallyWhenClicked),
                   subMenu);
}

void PopupMenu::addSubMenu (const String& subMenuName, const PopupMenu& subMenu, bool isActive)
{
    addSubMenu (subMenuName, subMenu, isActive, nullptr, false, 0);
}

void PopupMenu::addSubMenu (const String& subMenuName, const PopupMenu& subMenu, bool isActive,
                            const Image& iconToUse, bool isTicked, int itemResultID)
{
    addSubMenu (subMenuName, subMenu, isActive, createDrawableFromImage (iconToUse), isTicked, itemResultID);
}

void PopupMenu::addSubMenu (const String& subMenuName, const PopupMenu& subMenu, bool isActive,
                            Drawable* iconToUse, bool isTicked, int itemResultID)
{
    Item i;
    i.text = subMenuName;
    i.itemID = itemResultID;
    i.subMenu = new PopupMenu (subMenu);
    i.isEnabled = isActive && (itemResultID != 0 || subMenu.getNumItems() > 0);
    i.isTicked = isTicked;
    i.image = iconToUse;
    addItem (i);
}

void PopupMenu::addSeparator()
{
    if (items.size() > 0 && ! items.getLast()->isSeparator)
    {
        Item i;
        i.isSeparator = true;
        addItem (i);
    }
}

void PopupMenu::addSectionHeader (const String& title)
{
    Item i;
    i.text = title;
    i.isSectionHeader = true;
    addItem (i);
}

//==============================================================================
PopupMenu::Options::Options()
    : targetComponent (nullptr),
      parentComponent (nullptr),
      visibleItemID (0),
      minWidth (0),
      maxColumns (0),
      standardHeight (0)
{
    targetArea.setPosition (Desktop::getMousePosition());
}

PopupMenu::Options PopupMenu::Options::withTargetComponent (Component* comp) const noexcept
{
    Options o (*this);
    o.targetComponent = comp;

    if (comp != nullptr)
        o.targetArea = comp->getScreenBounds();

    return o;
}

PopupMenu::Options PopupMenu::Options::withTargetScreenArea (const Rectangle<int>& area) const noexcept
{
    Options o (*this);
    o.targetArea = area;
    return o;
}

PopupMenu::Options PopupMenu::Options::withMinimumWidth (int w) const noexcept
{
    Options o (*this);
    o.minWidth = w;
    return o;
}

PopupMenu::Options PopupMenu::Options::withMaximumNumColumns (int cols) const noexcept
{
    Options o (*this);
    o.maxColumns = cols;
    return o;
}

PopupMenu::Options PopupMenu::Options::withStandardItemHeight (int height) const noexcept
{
    Options o (*this);
    o.standardHeight = height;
    return o;
}

PopupMenu::Options PopupMenu::Options::withItemThatMustBeVisible (int idOfItemToBeVisible) const noexcept
{
    Options o (*this);
    o.visibleItemID = idOfItemToBeVisible;
    return o;
}

PopupMenu::Options PopupMenu::Options::withParentComponent (Component* parent) const noexcept
{
    Options o (*this);
    o.parentComponent = parent;
    return o;
}

Component* PopupMenu::createWindow (const Options& options,
                                    ApplicationCommandManager** managerOfChosenCommand) const
{
    if (items.size() > 0)
        return new HelperClasses::MenuWindow (*this, nullptr, options,
                                              ! options.targetArea.isEmpty(),
                                              ModifierKeys::getCurrentModifiers().isAnyMouseButtonDown(),
                                              managerOfChosenCommand);

    return nullptr;
}

//==============================================================================
// This invokes any command manager commands and deletes the menu window when it is dismissed
struct PopupMenuCompletionCallback  : public ModalComponentManager::Callback
{
    PopupMenuCompletionCallback()
        : managerOfChosenCommand (nullptr),
          prevFocused (Component::getCurrentlyFocusedComponent()),
          prevTopLevel (prevFocused != nullptr ? prevFocused->getTopLevelComponent() : nullptr)
    {
        PopupMenuSettings::menuWasHiddenBecauseOfAppChange = false;
    }

    void modalStateFinished (int result) override
    {
        if (managerOfChosenCommand != nullptr && result != 0)
        {
            ApplicationCommandTarget::InvocationInfo info (result);
            info.invocationMethod = ApplicationCommandTarget::InvocationInfo::fromMenu;

            managerOfChosenCommand->invoke (info, true);
        }

        // (this would be the place to fade out the component, if that's what's required)
        component = nullptr;

        if (! PopupMenuSettings::menuWasHiddenBecauseOfAppChange)
        {
            if (prevTopLevel != nullptr)
                prevTopLevel->toFront (true);

            if (prevFocused != nullptr)
                prevFocused->grabKeyboardFocus();
        }
    }

    ApplicationCommandManager* managerOfChosenCommand;
    ScopedPointer<Component> component;
    WeakReference<Component> prevFocused, prevTopLevel;

    JUCE_DECLARE_NON_COPYABLE (PopupMenuCompletionCallback)
};

int PopupMenu::showWithOptionalCallback (const Options& options, ModalComponentManager::Callback* const userCallback,
                                         const bool canBeModal)
{
    ScopedPointer<ModalComponentManager::Callback> userCallbackDeleter (userCallback);
    ScopedPointer<PopupMenuCompletionCallback> callback (new PopupMenuCompletionCallback());

    if (Component* window = createWindow (options, &(callback->managerOfChosenCommand)))
    {
        callback->component = window;

        window->setVisible (true); // (must be called before enterModalState on Windows to avoid DropShadower confusion)
        window->enterModalState (false, userCallbackDeleter.release());
        ModalComponentManager::getInstance()->attachCallback (window, callback.release());

        window->toFront (false);  // need to do this after making it modal, or it could
                                  // be stuck behind other comps that are already modal..

       #if JUCE_MODAL_LOOPS_PERMITTED
        if (userCallback == nullptr && canBeModal)
            return window->runModalLoop();
       #else
        ignoreUnused (canBeModal);
        jassert (! (userCallback == nullptr && canBeModal));
       #endif
    }

    return 0;
}

//==============================================================================
#if JUCE_MODAL_LOOPS_PERMITTED
int PopupMenu::showMenu (const Options& options)
{
    return showWithOptionalCallback (options, nullptr, true);
}
#endif

void PopupMenu::showMenuAsync (const Options& options, ModalComponentManager::Callback* userCallback)
{
   #if ! JUCE_MODAL_LOOPS_PERMITTED
    jassert (userCallback != nullptr);
   #endif

    showWithOptionalCallback (options, userCallback, false);
}

//==============================================================================
#if JUCE_MODAL_LOOPS_PERMITTED
int PopupMenu::show (const int itemIDThatMustBeVisible,
                     const int minimumWidth, const int maximumNumColumns,
                     const int standardItemHeight,
                     ModalComponentManager::Callback* callback)
{
    return showWithOptionalCallback (Options().withItemThatMustBeVisible (itemIDThatMustBeVisible)
                                              .withMinimumWidth (minimumWidth)
                                              .withMaximumNumColumns (maximumNumColumns)
                                              .withStandardItemHeight (standardItemHeight),
                                     callback, true);
}

int PopupMenu::showAt (const Rectangle<int>& screenAreaToAttachTo,
                       const int itemIDThatMustBeVisible,
                       const int minimumWidth, const int maximumNumColumns,
                       const int standardItemHeight,
                       ModalComponentManager::Callback* callback)
{
    return showWithOptionalCallback (Options().withTargetScreenArea (screenAreaToAttachTo)
                                              .withItemThatMustBeVisible (itemIDThatMustBeVisible)
                                              .withMinimumWidth (minimumWidth)
                                              .withMaximumNumColumns (maximumNumColumns)
                                              .withStandardItemHeight (standardItemHeight),
                                     callback, true);
}

int PopupMenu::showAt (Component* componentToAttachTo,
                       const int itemIDThatMustBeVisible,
                       const int minimumWidth, const int maximumNumColumns,
                       const int standardItemHeight,
                       ModalComponentManager::Callback* callback)
{
    Options options (Options().withItemThatMustBeVisible (itemIDThatMustBeVisible)
                              .withMinimumWidth (minimumWidth)
                              .withMaximumNumColumns (maximumNumColumns)
                              .withStandardItemHeight (standardItemHeight));

    if (componentToAttachTo != nullptr)
        options = options.withTargetComponent (componentToAttachTo);

    return showWithOptionalCallback (options, callback, true);
}
#endif

bool JUCE_CALLTYPE PopupMenu::dismissAllActiveMenus()
{
    const Array<HelperClasses::MenuWindow*>& windows = HelperClasses::MenuWindow::getActiveWindows();
    const int numWindows = windows.size();

    for (int i = numWindows; --i >= 0;)
        if (HelperClasses::MenuWindow* const pmw = windows[i])
            pmw->dismissMenu (nullptr);

    return numWindows > 0;
}

//==============================================================================
int PopupMenu::getNumItems() const noexcept
{
    int num = 0;

    for (int i = items.size(); --i >= 0;)
        if (! items.getUnchecked(i)->isSeparator)
            ++num;

    return num;
}

bool PopupMenu::containsCommandItem (const int commandID) const
{
    for (int i = items.size(); --i >= 0;)
    {
        const Item& mi = *items.getUnchecked (i);

        if ((mi.itemID == commandID && mi.commandManager != nullptr)
              || (mi.subMenu != nullptr && mi.subMenu->containsCommandItem (commandID)))
            return true;
    }

    return false;
}

bool PopupMenu::containsAnyActiveItems() const noexcept
{
    for (int i = items.size(); --i >= 0;)
    {
        const Item& mi = *items.getUnchecked (i);

        if (mi.subMenu != nullptr)
        {
            if (mi.subMenu->containsAnyActiveItems())
                return true;
        }
        else if (mi.isEnabled)
        {
            return true;
        }
    }

    return false;
}

void PopupMenu::setLookAndFeel (LookAndFeel* const newLookAndFeel)
{
    lookAndFeel = newLookAndFeel;
}

//==============================================================================
PopupMenu::CustomComponent::CustomComponent (bool autoTrigger)
    : isHighlighted (false),
      triggeredAutomatically (autoTrigger)
{
}

PopupMenu::CustomComponent::~CustomComponent()
{
}

void PopupMenu::CustomComponent::setHighlighted (bool shouldBeHighlighted)
{
    isHighlighted = shouldBeHighlighted;
    repaint();
}

void PopupMenu::CustomComponent::triggerMenuItem()
{
    if (HelperClasses::ItemComponent* const mic = findParentComponentOfClass<HelperClasses::ItemComponent>())
    {
        if (HelperClasses::MenuWindow* const pmw = mic->findParentComponentOfClass<HelperClasses::MenuWindow>())
        {
            pmw->dismissMenu (&mic->item);
        }
        else
        {
            // something must have gone wrong with the component hierarchy if this happens..
            jassertfalse;
        }
    }
    else
    {
        // why isn't this component inside a menu? Not much point triggering the item if
        // there's no menu.
        jassertfalse;
    }
}

//==============================================================================
PopupMenu::MenuItemIterator::MenuItemIterator (const PopupMenu& m)  : menu (m), index (0) {}
PopupMenu::MenuItemIterator::~MenuItemIterator() {}

bool PopupMenu::MenuItemIterator::next()
{
    if (index >= menu.items.size())
        return false;

    const Item* const item = menu.items.getUnchecked (index++);

    return ! (item->isSeparator && index >= menu.items.size()); // (avoid showing a separator at the end)
}

const PopupMenu::Item& PopupMenu::MenuItemIterator::getItem() const noexcept
{
    jassert (isPositiveAndBelow (index - 1, menu.items.size()));
    return *menu.items.getUnchecked (index - 1);
}
