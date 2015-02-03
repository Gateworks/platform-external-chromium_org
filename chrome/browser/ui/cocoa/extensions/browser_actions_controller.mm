// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/extensions/browser_actions_controller.h"

#include <cmath>
#include <string>

#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/extensions/extension_action.h"
#include "chrome/browser/extensions/extension_action_manager.h"
#include "chrome/browser/extensions/extension_toolbar_model.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#import "chrome/browser/ui/cocoa/extensions/browser_action_button.h"
#import "chrome/browser/ui/cocoa/extensions/browser_actions_container_view.h"
#import "chrome/browser/ui/cocoa/extensions/extension_popup_controller.h"
#import "chrome/browser/ui/cocoa/image_button_cell.h"
#import "chrome/browser/ui/cocoa/menu_button.h"
#include "chrome/browser/ui/extensions/extension_action_view_controller.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "grit/theme_resources.h"
#import "third_party/google_toolbox_for_mac/src/AppKit/GTMNSAnimation+Duration.h"

using extensions::Extension;
using extensions::ExtensionList;

NSString* const kBrowserActionVisibilityChangedNotification =
    @"BrowserActionVisibilityChangedNotification";

namespace {
const CGFloat kAnimationDuration = 0.2;

const CGFloat kChevronWidth = 18;

// Since the container is the maximum height of the toolbar, we have
// to move the buttons up by this amount in order to have them look
// vertically centered within the toolbar.
const CGFloat kBrowserActionOriginYOffset = 5.0;

// The size of each button on the toolbar.
const CGFloat kBrowserActionHeight = 29.0;
const CGFloat kBrowserActionWidth = 29.0;

// The padding between browser action buttons.
const CGFloat kBrowserActionButtonPadding = 2.0;

// Padding between Omnibox and first button.  Since the buttons have a
// pixel of internal padding, this needs an extra pixel.
const CGFloat kBrowserActionLeftPadding = kBrowserActionButtonPadding + 1.0;

// How far to inset from the bottom of the view to get the top border
// of the popup 2px below the bottom of the Omnibox.
const CGFloat kBrowserActionBubbleYOffset = 3.0;

}  // namespace

@interface BrowserActionsController(Private)
// Used during initialization to create the BrowserActionButton objects from the
// stored toolbar model.
- (void)createButtons;

// Creates and then adds the given extension's action button to the container
// at the given index within the container. It does not affect the toolbar model
// object since it is called when the toolbar model changes.
- (void)createActionButtonForExtension:(const Extension*)extension
                             withIndex:(NSUInteger)index;

// Removes an action button for the given extension from the container. This
// method also does not affect the underlying toolbar model since it is called
// when the toolbar model changes.
- (void)removeActionButtonForExtension:(const Extension*)extension;

// Useful in the case of a Browser Action being added/removed from the middle of
// the container, this method repositions each button according to the current
// toolbar model.
- (void)positionActionButtonsAndAnimate:(BOOL)animate;

// During container resizing, buttons become more transparent as they are pushed
// off the screen. This method updates each button's opacity determined by the
// position of the button.
- (void)updateButtonOpacity;

// Returns the existing button associated with the given id; nil if it cannot be
// found.
- (BrowserActionButton*)buttonForId:(const std::string&)id;

// Returns the preferred width of the container given the number of visible
// buttons |buttonCount|.
- (CGFloat)containerWidthWithButtonCount:(NSUInteger)buttonCount;

// Returns the number of buttons that can fit in the container according to its
// current size.
- (NSUInteger)containerButtonCapacity;

// Notification handlers for events registered by the class.

// Updates each button's opacity, the cursor rects and chevron position.
- (void)containerFrameChanged:(NSNotification*)notification;

// Hides the chevron and unhides every hidden button so that dragging the
// container out smoothly shows the Browser Action buttons.
- (void)containerDragStart:(NSNotification*)notification;

// Sends a notification for the toolbar to reposition surrounding UI elements.
- (void)containerDragging:(NSNotification*)notification;

// Determines which buttons need to be hidden based on the new size, hides them
// and updates the chevron overflow menu. Also fires a notification to let the
// toolbar know that the drag has finished.
- (void)containerDragFinished:(NSNotification*)notification;

// Sends a notification for the toolbar to determine whether the container can
// translate with a delta on x-axis.
- (void)containerWillTranslateOnX:(NSNotification*)notification;

// Adjusts the position of the surrounding action buttons depending on where the
// button is within the container.
- (void)actionButtonDragging:(NSNotification*)notification;

// Updates the position of the Browser Actions within the container. This fires
// when _any_ Browser Action button is done dragging to keep all open windows in
// sync visually.
- (void)actionButtonDragFinished:(NSNotification*)notification;

// Moves the given button both visually and within the toolbar model to the
// specified index.
- (void)moveButton:(BrowserActionButton*)button
           toIndex:(NSUInteger)index
           animate:(BOOL)animate;

// Handles when the given BrowserActionButton object is clicked and whether
// it should grant tab permissions. API-simulated clicks should not grant.
- (BOOL)browserActionClicked:(BrowserActionButton*)button
                 shouldGrant:(BOOL)shouldGrant;
- (BOOL)browserActionClicked:(BrowserActionButton*)button;

// The reason |frame| is specified in these chevron functions is because the
// container may be animating and the end frame of the animation should be
// passed instead of the current frame (which may be off and cause the chevron
// to jump at the end of its animation).

// Shows the overflow chevron button depending on whether there are any hidden
// extensions within the frame given.
- (void)showChevronIfNecessaryInFrame:(NSRect)frame animate:(BOOL)animate;

// Moves the chevron to its correct position within |frame|.
- (void)updateChevronPositionInFrame:(NSRect)frame;

// Shows or hides the chevron, animating as specified by |animate|.
- (void)setChevronHidden:(BOOL)hidden
                 inFrame:(NSRect)frame
                 animate:(BOOL)animate;

// Handles when a menu item within the chevron overflow menu is selected.
- (void)chevronItemSelected:(id)menuItem;

// Updates the container's grippy cursor based on the number of hidden buttons.
- (void)updateGrippyCursors;
@end

// A helper class to proxy extension notifications to the view controller's
// appropriate methods.
class ExtensionServiceObserverBridge
    : public extensions::ExtensionToolbarModel::Observer {
 public:
  ExtensionServiceObserverBridge(BrowserActionsController* owner,
                                 Browser* browser)
    : owner_(owner), browser_(browser) {
  }

  // extensions::ExtensionToolbarModel::Observer implementation.
  void ToolbarExtensionAdded(const Extension* extension, int index) override {
    [owner_ createActionButtonForExtension:extension withIndex:index];
    [owner_ resizeContainerAndAnimate:NO];
  }

  void ToolbarExtensionRemoved(const Extension* extension) override {
    [owner_ removeActionButtonForExtension:extension];
    [owner_ resizeContainerAndAnimate:NO];
  }

  void ToolbarExtensionMoved(const Extension* extension, int index) override {}

  void ToolbarExtensionUpdated(const Extension* extension) override {
    BrowserActionButton* button = [owner_ buttonForId:extension->id()];
    if (button)
      [button updateState];
  }

  bool ShowExtensionActionPopup(const Extension* extension,
                                bool grant_active_tab) override {
    // Do not override other popups and only show in active window.
    ExtensionPopupController* popup = [ExtensionPopupController popup];
    if (popup || !browser_->window()->IsActive())
      return false;

    BrowserActionButton* button = [owner_ buttonForId:extension->id()];
    return button && [button viewController]->ExecuteAction(grant_active_tab);
  }

  void ToolbarVisibleCountChanged() override {}

  void OnToolbarReorderNecessary(content::WebContents* web_contents) override {
    // TODO(devlin): Implement on mac.
  }

  void ToolbarHighlightModeChanged(bool is_highlighting) override {}

  void OnToolbarModelInitialized() override {
    [owner_ createButtons];
  }

  Browser* GetBrowser() override { return browser_; }

 private:
  // The object we need to inform when we get a notification. Weak. Owns us.
  BrowserActionsController* owner_;

  // The browser we listen for events from. Weak.
  Browser* browser_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionServiceObserverBridge);
};

@implementation BrowserActionsController

@synthesize containerView = containerView_;

#pragma mark -
#pragma mark Public Methods

- (id)initWithBrowser:(Browser*)browser
        containerView:(BrowserActionsContainerView*)container {
  DCHECK(browser && container);

  if ((self = [super init])) {
    browser_ = browser;
    profile_ = browser->profile();

    observer_.reset(new ExtensionServiceObserverBridge(self, browser_));
    toolbarModel_ = extensions::ExtensionToolbarModel::Get(profile_);
    if (toolbarModel_)
      toolbarModel_->AddObserver(observer_.get());

    containerView_ = container;
    [containerView_ setPostsFrameChangedNotifications:YES];
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(containerFrameChanged:)
               name:NSViewFrameDidChangeNotification
             object:containerView_];
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(containerDragStart:)
               name:kBrowserActionGrippyDragStartedNotification
             object:containerView_];
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(containerDragging:)
               name:kBrowserActionGrippyDraggingNotification
             object:containerView_];
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(containerDragFinished:)
               name:kBrowserActionGrippyDragFinishedNotification
             object:containerView_];
    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(containerWillTranslateOnX:)
               name:kBrowserActionGrippyWillDragNotification
             object:containerView_];
    // Listen for a finished drag from any button to make sure each open window
    // stays in sync.
    [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(actionButtonDragFinished:)
             name:kBrowserActionButtonDragEndNotification
           object:nil];

    chevronAnimation_.reset([[NSViewAnimation alloc] init]);
    [chevronAnimation_ gtm_setDuration:kAnimationDuration
                             eventMask:NSLeftMouseUpMask];
    [chevronAnimation_ setAnimationBlockingMode:NSAnimationNonblocking];

    hiddenButtons_.reset([[NSMutableArray alloc] init]);
    buttons_.reset([[NSMutableDictionary alloc] init]);
    if (toolbarModel_ && toolbarModel_->extensions_initialized())
      [self createButtons];
    [self showChevronIfNecessaryInFrame:[containerView_ frame] animate:NO];
    [self updateGrippyCursors];
    [container setResizable:!profile_->IsOffTheRecord()];
  }

  return self;
}

- (void)dealloc {
  if (toolbarModel_)
    toolbarModel_->RemoveObserver(observer_.get());
  for (BrowserActionButton* button in [buttons_ allValues]) {
    [button removeFromSuperview];
    [button onRemoved];
  }
  [hiddenButtons_ removeAllObjects];
  [buttons_ removeAllObjects];

  [[NSNotificationCenter defaultCenter] removeObserver:self];
  [super dealloc];
}

- (void)update {
  for (BrowserActionButton* button in [buttons_ allValues])
    [button updateState];
}

- (NSUInteger)buttonCount {
  return [buttons_ count];
}

- (NSUInteger)visibleButtonCount {
  return [self buttonCount] - [hiddenButtons_ count];
}

- (void)resizeContainerAndAnimate:(BOOL)animate {
  int iconCount = toolbarModel_->visible_icon_count();

  [containerView_ resizeToWidth:[self containerWidthWithButtonCount:iconCount]
                        animate:animate];
  NSRect frame = animate ? [containerView_ animationEndFrame] :
                           [containerView_ frame];

  [self showChevronIfNecessaryInFrame:frame animate:animate];

  if (!animate) {
    [[NSNotificationCenter defaultCenter]
        postNotificationName:kBrowserActionVisibilityChangedNotification
                      object:self];
  }
}

- (CGFloat)savedWidth {
  if (!toolbarModel_)
    return 0;

  int savedButtonCount = toolbarModel_->visible_icon_count();
  if (static_cast<NSUInteger>(savedButtonCount) > [self buttonCount])
    savedButtonCount = [self buttonCount];
  return [self containerWidthWithButtonCount:savedButtonCount];
}

- (NSPoint)popupPointForId:(const std::string&)id {
  NSButton* button = [self buttonForId:id];
  if (!button)
    return NSZeroPoint;

  if ([hiddenButtons_ containsObject:button])
    button = chevronMenuButton_.get();

  // Anchor point just above the center of the bottom.
  const NSRect bounds = [button bounds];
  DCHECK([button isFlipped]);
  NSPoint anchor = NSMakePoint(NSMidX(bounds),
                               NSMaxY(bounds) - kBrowserActionBubbleYOffset);
  return [button convertPoint:anchor toView:nil];
}

- (BOOL)chevronIsHidden {
  if (!chevronMenuButton_.get())
    return YES;

  if (![chevronAnimation_ isAnimating])
    return [chevronMenuButton_ isHidden];

  DCHECK([[chevronAnimation_ viewAnimations] count] > 0);

  // The chevron is animating in or out. Determine which one and have the return
  // value reflect where the animation is headed.
  NSString* effect = [[[chevronAnimation_ viewAnimations] objectAtIndex:0]
      valueForKey:NSViewAnimationEffectKey];
  if (effect == NSViewAnimationFadeInEffect) {
    return NO;
  } else if (effect == NSViewAnimationFadeOutEffect) {
    return YES;
  }

  NOTREACHED();
  return YES;
}

- (void)activateBrowserAction:(const std::string&)id {
  BrowserActionButton* button = [self buttonForId:id];
  // |button| can be nil when the browser action has its button hidden.
  if (button)
    [self browserActionClicked:button];
}

- (content::WebContents*)currentWebContents {
  return browser_->tab_strip_model()->GetActiveWebContents();
}

#pragma mark -
#pragma mark NSMenuDelegate

- (void)menuNeedsUpdate:(NSMenu*)menu {
  [menu removeAllItems];

  // See menu_button.h for documentation on why this is needed.
  [menu addItemWithTitle:@"" action:nil keyEquivalent:@""];

  for (BrowserActionButton* button in hiddenButtons_.get()) {
    NSString* name =
        base::SysUTF16ToNSString([button viewController]->GetActionName());
    NSMenuItem* item =
        [menu addItemWithTitle:name
                        action:@selector(chevronItemSelected:)
                 keyEquivalent:@""];
    [item setRepresentedObject:button];
    [item setImage:[button compositedImage]];
    [item setTarget:self];
    [item setEnabled:[button isEnabled]];
  }
}

#pragma mark -
#pragma mark Private Methods

- (void)createButtons {
  if (!toolbarModel_)
    return;

  NSUInteger i = 0;
  for (ExtensionList::const_iterator iter =
           toolbarModel_->toolbar_items().begin();
       iter != toolbarModel_->toolbar_items().end(); ++iter) {
    [self createActionButtonForExtension:iter->get() withIndex:i++];
  }

  [self resizeContainerAndAnimate:NO];
}

- (void)createActionButtonForExtension:(const Extension*)extension
                             withIndex:(NSUInteger)index {
  // Show the container if it's the first button. Otherwise it will be shown
  // already.
  if ([self buttonCount] == 0)
    [containerView_ setHidden:NO];

  NSRect buttonFrame = NSMakeRect(0.0, kBrowserActionOriginYOffset,
                                  kBrowserActionWidth, kBrowserActionHeight);
  ExtensionAction* extensionAction =
      extensions::ExtensionActionManager::Get(browser_->profile())->
          GetExtensionAction(*extension);
  DCHECK(extensionAction)
        << "Don't create a BrowserActionButton if there is no browser action.";
  scoped_ptr<ToolbarActionViewController> viewController(
      new ExtensionActionViewController(extension, browser_, extensionAction));
  BrowserActionButton* newButton =
      [[[BrowserActionButton alloc]
         initWithFrame:buttonFrame
        viewController:viewController.Pass()
            controller:self] autorelease];
  [newButton setTarget:self];
  [newButton setAction:@selector(browserActionClicked:)];
  NSString* buttonKey = base::SysUTF8ToNSString(extension->id());
  if (!buttonKey)
    return;
  [buttons_ setObject:newButton forKey:buttonKey];

  [self positionActionButtonsAndAnimate:NO];

  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(actionButtonDragging:)
             name:kBrowserActionButtonDraggingNotification
           object:newButton];


  [containerView_ setMaxWidth:
      [self containerWidthWithButtonCount:[self buttonCount]]];
  [containerView_ setNeedsDisplay:YES];
}

- (void)removeActionButtonForExtension:(const Extension*)extension {
  NSString* buttonKey = base::SysUTF8ToNSString(extension->id());
  if (!buttonKey)
    return;

  BrowserActionButton* button = [buttons_ objectForKey:buttonKey];

  [button removeFromSuperview];
  [button onRemoved];
  // It may or may not be hidden, but it won't matter to NSMutableArray either
  // way.
  [hiddenButtons_ removeObject:button];

  [buttons_ removeObjectForKey:buttonKey];
  if ([self buttonCount] == 0) {
    // No more buttons? Hide the container.
    [containerView_ setHidden:YES];
  } else {
    [self positionActionButtonsAndAnimate:NO];
  }
  [containerView_ setMaxWidth:
      [self containerWidthWithButtonCount:[self buttonCount]]];
  [containerView_ setNeedsDisplay:YES];
}

- (void)positionActionButtonsAndAnimate:(BOOL)animate {
  NSUInteger i = 0;
  for (ExtensionList::const_iterator iter =
           toolbarModel_->toolbar_items().begin();
       iter != toolbarModel_->toolbar_items().end(); ++iter) {
    BrowserActionButton* button = [self buttonForId:(iter->get()->id())];
    if (!button)
      continue;
    if (![button isBeingDragged])
      [self moveButton:button toIndex:i animate:animate];
    ++i;
  }
}

- (void)updateButtonOpacity {
  for (BrowserActionButton* button in [buttons_ allValues]) {
    NSRect buttonFrame = [button frame];
    if (NSContainsRect([containerView_ bounds], buttonFrame)) {
      if ([button alphaValue] != 1.0)
        [button setAlphaValue:1.0];

      continue;
    }
    CGFloat intersectionWidth =
        NSWidth(NSIntersectionRect([containerView_ bounds], buttonFrame));
    CGFloat alpha = std::max(static_cast<CGFloat>(0.0),
                             intersectionWidth / NSWidth(buttonFrame));
    [button setAlphaValue:alpha];
    [button setNeedsDisplay:YES];
  }
}

- (BrowserActionButton*)buttonForId:(const std::string&)id {
  NSString* nsId = base::SysUTF8ToNSString(id);
  DCHECK(nsId);
  if (!nsId)
    return nil;
  return [buttons_ objectForKey:nsId];
}

- (CGFloat)containerWidthWithButtonCount:(NSUInteger)buttonCount {
  // Left-side padding which works regardless of whether a button or
  // chevron leads.
  CGFloat width = kBrowserActionLeftPadding;

  // Include the buttons and padding between.
  if (buttonCount > 0) {
    width += buttonCount * kBrowserActionWidth;
    width += (buttonCount - 1) * kBrowserActionButtonPadding;
  }

  // Make room for the chevron if any buttons are hidden.
  if ([self buttonCount] != [self visibleButtonCount]) {
    // Chevron and buttons both include 1px padding w/in their bounds,
    // so this leaves 2px between the last browser action and chevron,
    // and also works right if the chevron is the only button.
    width += kChevronWidth;
  }

  return width;
}

- (NSUInteger)containerButtonCapacity {
  // Edge-to-edge span of the browser action buttons.
  CGFloat actionSpan = [self savedWidth] - kBrowserActionLeftPadding;

  // Add in some padding for the browser action on the end, then
  // divide out to get the number of action buttons that fit.
  return (actionSpan + kBrowserActionButtonPadding) /
      (kBrowserActionWidth + kBrowserActionButtonPadding);
}

- (void)containerFrameChanged:(NSNotification*)notification {
  [self updateButtonOpacity];
  [[containerView_ window] invalidateCursorRectsForView:containerView_];
  [self updateChevronPositionInFrame:[containerView_ frame]];
}

- (void)containerDragStart:(NSNotification*)notification {
  [self setChevronHidden:YES inFrame:[containerView_ frame] animate:YES];
  while([hiddenButtons_ count] > 0) {
    BrowserActionButton* button = [hiddenButtons_ objectAtIndex:0];
    [button setAlphaValue:1.0];
    [containerView_ addSubview:button];
    [hiddenButtons_ removeObjectAtIndex:0];
  }
}

- (void)containerDragging:(NSNotification*)notification {
  [[NSNotificationCenter defaultCenter]
      postNotificationName:kBrowserActionGrippyDraggingNotification
                    object:self];
}

- (void)containerDragFinished:(NSNotification*)notification {
  for (ExtensionList::const_iterator iter =
           toolbarModel_->toolbar_items().begin();
       iter != toolbarModel_->toolbar_items().end(); ++iter) {
    BrowserActionButton* button = [self buttonForId:(iter->get()->id())];
    NSRect buttonFrame = [button frame];
    if (NSContainsRect([containerView_ bounds], buttonFrame))
      continue;

    CGFloat intersectionWidth =
        NSWidth(NSIntersectionRect([containerView_ bounds], buttonFrame));
    // Pad the threshold by 5 pixels in order to have the buttons hide more
    // easily.
    if (([containerView_ grippyPinned] && intersectionWidth > 0) ||
        (intersectionWidth <= (NSWidth(buttonFrame) / 2) + 5.0)) {
      [button setAlphaValue:0.0];
      [button removeFromSuperview];
      [hiddenButtons_ addObject:button];
    }
  }
  [self updateGrippyCursors];

  toolbarModel_->SetVisibleIconCount([self visibleButtonCount]);

  [[NSNotificationCenter defaultCenter]
      postNotificationName:kBrowserActionGrippyDragFinishedNotification
                    object:self];
}

- (void)containerWillTranslateOnX:(NSNotification*)notification {
  [[NSNotificationCenter defaultCenter]
      postNotificationName:kBrowserActionGrippyWillDragNotification
                    object:self
                  userInfo:notification.userInfo];
}

- (void)actionButtonDragging:(NSNotification*)notification {
  if (![self chevronIsHidden])
    [self setChevronHidden:YES inFrame:[containerView_ frame] animate:YES];

  // Determine what index the dragged button should lie in, alter the model and
  // reposition the buttons.
  CGFloat dragThreshold = std::floor(kBrowserActionWidth / 2);
  BrowserActionButton* draggedButton = [notification object];
  NSRect draggedButtonFrame = [draggedButton frame];

  NSUInteger index = 0;
  for (ExtensionList::const_iterator iter =
           toolbarModel_->toolbar_items().begin();
       iter != toolbarModel_->toolbar_items().end(); ++iter) {
    BrowserActionButton* button = [self buttonForId:(iter->get()->id())];
    CGFloat intersectionWidth =
        NSWidth(NSIntersectionRect(draggedButtonFrame, [button frame]));

    if (intersectionWidth > dragThreshold && button != draggedButton &&
        ![button isAnimating] && index < [self visibleButtonCount]) {
      toolbarModel_->MoveExtensionIcon([draggedButton viewController]->GetId(),
                                       index);
      [self positionActionButtonsAndAnimate:YES];
      return;
    }
    ++index;
  }
}

- (void)actionButtonDragFinished:(NSNotification*)notification {
  [self showChevronIfNecessaryInFrame:[containerView_ frame] animate:YES];
  [self positionActionButtonsAndAnimate:YES];
}

- (void)moveButton:(BrowserActionButton*)button
           toIndex:(NSUInteger)index
           animate:(BOOL)animate {
  CGFloat xOffset = kBrowserActionLeftPadding +
      (index * (kBrowserActionWidth + kBrowserActionButtonPadding));
  NSRect buttonFrame = [button frame];
  buttonFrame.origin.x = xOffset;
  [button setFrame:buttonFrame animate:animate];

  if (index < [self containerButtonCapacity]) {
    // Make sure the button is within the visible container.
    if ([button superview] != containerView_) {
      [containerView_ addSubview:button];
      [button setAlphaValue:1.0];
      [hiddenButtons_ removeObjectIdenticalTo:button];
    }
  } else if (![hiddenButtons_ containsObject:button]) {
    [hiddenButtons_ addObject:button];
    [button removeFromSuperview];
    [button setAlphaValue:0.0];
  }
}

- (BOOL)browserActionClicked:(BrowserActionButton*)button
                 shouldGrant:(BOOL)shouldGrant {
  return [button viewController]->ExecuteAction(shouldGrant);
}

- (BOOL)browserActionClicked:(BrowserActionButton*)button {
  return [self browserActionClicked:button
                        shouldGrant:YES];
}

- (void)showChevronIfNecessaryInFrame:(NSRect)frame animate:(BOOL)animate {
  [self setChevronHidden:([self buttonCount] == [self visibleButtonCount])
                 inFrame:frame
                 animate:animate];
}

- (void)updateChevronPositionInFrame:(NSRect)frame {
  CGFloat xPos = NSWidth(frame) - kChevronWidth;
  NSRect buttonFrame = NSMakeRect(xPos,
                                  kBrowserActionOriginYOffset,
                                  kChevronWidth,
                                  kBrowserActionHeight);
  [chevronMenuButton_ setFrame:buttonFrame];
}

- (void)setChevronHidden:(BOOL)hidden
                 inFrame:(NSRect)frame
                 animate:(BOOL)animate {
  if (hidden == [self chevronIsHidden])
    return;

  if (!chevronMenuButton_.get()) {
    chevronMenuButton_.reset([[MenuButton alloc] init]);
    [chevronMenuButton_ setOpenMenuOnClick:YES];
    [chevronMenuButton_ setBordered:NO];
    [chevronMenuButton_ setShowsBorderOnlyWhileMouseInside:YES];

    [[chevronMenuButton_ cell] setImageID:IDR_BROWSER_ACTIONS_OVERFLOW
                           forButtonState:image_button_cell::kDefaultState];
    [[chevronMenuButton_ cell] setImageID:IDR_BROWSER_ACTIONS_OVERFLOW_H
                           forButtonState:image_button_cell::kHoverState];
    [[chevronMenuButton_ cell] setImageID:IDR_BROWSER_ACTIONS_OVERFLOW_P
                           forButtonState:image_button_cell::kPressedState];

    overflowMenu_.reset([[NSMenu alloc] initWithTitle:@""]);
    [overflowMenu_ setAutoenablesItems:NO];
    [overflowMenu_ setDelegate:self];
    [chevronMenuButton_ setAttachedMenu:overflowMenu_];

    [containerView_ addSubview:chevronMenuButton_];
  }

  [self updateChevronPositionInFrame:frame];

  // Stop any running animation.
  [chevronAnimation_ stopAnimation];

  if (!animate) {
    [chevronMenuButton_ setHidden:hidden];
    return;
  }

  NSDictionary* animationDictionary;
  if (hidden) {
    animationDictionary = [NSDictionary dictionaryWithObjectsAndKeys:
        chevronMenuButton_.get(), NSViewAnimationTargetKey,
        NSViewAnimationFadeOutEffect, NSViewAnimationEffectKey,
        nil];
  } else {
    [chevronMenuButton_ setHidden:NO];
    animationDictionary = [NSDictionary dictionaryWithObjectsAndKeys:
        chevronMenuButton_.get(), NSViewAnimationTargetKey,
        NSViewAnimationFadeInEffect, NSViewAnimationEffectKey,
        nil];
  }
  [chevronAnimation_ setViewAnimations:
      [NSArray arrayWithObject:animationDictionary]];
  [chevronAnimation_ startAnimation];
}

- (void)chevronItemSelected:(id)menuItem {
  [self browserActionClicked:[menuItem representedObject]];
}

- (void)updateGrippyCursors {
  [containerView_ setCanDragLeft:[hiddenButtons_ count] > 0];
  [containerView_ setCanDragRight:[self visibleButtonCount] > 0];
  [[containerView_ window] invalidateCursorRectsForView:containerView_];
}

#pragma mark -
#pragma mark Testing Methods

- (BrowserActionButton*)buttonWithIndex:(NSUInteger)index {
  const extensions::ExtensionList& toolbar_items =
      toolbarModel_->toolbar_items();
  if (index < toolbar_items.size()) {
    const Extension* extension = toolbar_items[index].get();
    return [buttons_ objectForKey:base::SysUTF8ToNSString(extension->id())];
  }
  return nil;
}

@end
