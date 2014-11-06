# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
from telemetry.page import page as page_module
from telemetry.page import page_set as page_set_module


def _GetCurrentLocation(action_runner):
  return action_runner.EvaluateJavaScript('document.location.href')


def _WaitForLocationChange(action_runner, old_href):
  action_runner.WaitForJavaScriptCondition(
      'document.location.href != "%s"' % old_href)


class Top25Page(page_module.Page):

  def __init__(self, url, page_set, name='', credentials=None):
    super(Top25Page, self).__init__(
        url=url, page_set=page_set, name=name,
        credentials_path='data/credentials.json')
    self.user_agent_type = 'desktop'
    self.archive_data_file = 'data/top_25.json'
    self.credentials = credentials

  def RunSmoothness(self, action_runner):
    interaction = action_runner.BeginGestureInteraction(
        'ScrollAction', is_smooth=True)
    action_runner.ScrollPage()
    interaction.End()

  def RunRepaint(self, action_runner):
    action_runner.RepaintContinuously(seconds=5)


class GoogleWebSearchPage(Top25Page):

  """ Why: top google property; a google tab is often open """

  def __init__(self, page_set):
    super(GoogleWebSearchPage, self).__init__(
        url='https://www.google.com/#hl=en&q=barack+obama',
        page_set=page_set)

  def RunNavigateSteps(self, action_runner):
    action_runner.NavigateToPage(self)
    action_runner.WaitForElement(text='Next')


class GmailPage(Top25Page):

  """ Why: productivity, top google properties """

  def __init__(self, page_set):
    super(GmailPage, self).__init__(
        url='https://mail.google.com/mail/',
        page_set=page_set,
        credentials='google')

  def RunNavigateSteps(self, action_runner):
    action_runner.NavigateToPage(self)
    action_runner.WaitForJavaScriptCondition(
        'window.gmonkey !== undefined &&'
        'document.getElementById("gb") !== null')

  def RunSmoothness(self, action_runner):
    action_runner.ExecuteJavaScript('''
        gmonkey.load('2.0', function(api) {
          window.__scrollableElementForTelemetry = api.getScrollableElement();
        });''')
    action_runner.WaitForJavaScriptCondition(
        'window.__scrollableElementForTelemetry != null')
    interaction = action_runner.BeginGestureInteraction(
        'ScrollAction', is_smooth=True)
    action_runner.ScrollElement(
        element_function='window.__scrollableElementForTelemetry')
    interaction.End()


class GoogleCalendarPage(Top25Page):

  """ Why: productivity, top google properties """

  def __init__(self, page_set):
    super(GoogleCalendarPage, self).__init__(
        url='https://www.google.com/calendar/',
        page_set=page_set,
        credentials='google')

  def RunNavigateSteps(self, action_runner):
    action_runner.NavigateToPage(self)
    action_runner.Wait(2)
    action_runner.WaitForElement('div[class~="navForward"]')
    action_runner.ExecuteJavaScript('''
        (function() {
          var elem = document.createElement('meta');
          elem.name='viewport';
          elem.content='initial-scale=1';
          document.body.appendChild(elem);
        })();''')
    action_runner.Wait(1)

  def RunSmoothness(self, action_runner):
    interaction = action_runner.BeginGestureInteraction(
        'ScrollAction', is_smooth=True)
    action_runner.ScrollElement(selector='#scrolltimedeventswk')
    interaction.End()


class GoogleDocPage(Top25Page):

  """ Why: productivity, top google properties; Sample doc in the link """

  def __init__(self, page_set):
    super(GoogleDocPage, self).__init__(
        # pylint: disable=C0301
        url='https://docs.google.com/document/d/1X-IKNjtEnx-WW5JIKRLsyhz5sbsat3mfTpAPUSX3_s4/view',
        page_set=page_set,
        name='Docs  (1 open document tab)',
        credentials='google')

  def RunNavigateSteps(self, action_runner):
    action_runner.NavigateToPage(self)
    action_runner.Wait(2)
    action_runner.WaitForJavaScriptCondition(
        'document.getElementsByClassName("kix-appview-editor").length')

  def RunSmoothness(self, action_runner):
    interaction = action_runner.BeginGestureInteraction(
        'ScrollAction', is_smooth=True)
    action_runner.ScrollElement(selector='.kix-appview-editor')
    interaction.End()


class GooglePlusPage(Top25Page):

  """ Why: social; top google property; Public profile; infinite scrolls """

  def __init__(self, page_set):
    super(GooglePlusPage, self).__init__(
        url='https://plus.google.com/110031535020051778989/posts',
        page_set=page_set,
        credentials='google')

  def RunNavigateSteps(self, action_runner):
    action_runner.NavigateToPage(self)
    action_runner.WaitForElement(text='Home')

  def RunSmoothness(self, action_runner):
    interaction = action_runner.BeginGestureInteraction(
        'ScrollAction', is_smooth=True)
    action_runner.ScrollPage()
    interaction.End()


class YoutubePage(Top25Page):

  """ Why: #3 (Alexa global) """

  def __init__(self, page_set):
    super(YoutubePage, self).__init__(
        url='http://www.youtube.com',
        page_set=page_set, credentials='google')

  def RunNavigateSteps(self, action_runner):
    action_runner.NavigateToPage(self)
    action_runner.Wait(2)


class BlogspotPage(Top25Page):

  """ Why: #11 (Alexa global), google property; some blogger layouts have
  infinite scroll but more interesting """

  def __init__(self, page_set):
    super(BlogspotPage, self).__init__(
        url='http://googlewebmastercentral.blogspot.com/',
        page_set=page_set,
        name='Blogger')

  def RunNavigateSteps(self, action_runner):
    action_runner.NavigateToPage(self)
    action_runner.WaitForElement(text='accessibility')


class WordpressPage(Top25Page):

  """ Why: #18 (Alexa global), Picked an interesting post """

  def __init__(self, page_set):
    super(WordpressPage, self).__init__(
        # pylint: disable=C0301
        url='http://en.blog.wordpress.com/2012/09/04/freshly-pressed-editors-picks-for-august-2012/',
        page_set=page_set,
        name='Wordpress')

  def RunNavigateSteps(self, action_runner):
    action_runner.NavigateToPage(self)
    action_runner.WaitForElement(
        # pylint: disable=C0301
        'a[href="http://en.blog.wordpress.com/2012/08/30/new-themes-able-and-sight/"]')


class FacebookPage(Top25Page):

  """ Why: top social,Public profile """

  def __init__(self, page_set):
    super(FacebookPage, self).__init__(
        url='https://www.facebook.com/barackobama',
        page_set=page_set,
        name='Facebook', credentials='facebook2')

  def RunNavigateSteps(self, action_runner):
    action_runner.NavigateToPage(self)
    action_runner.WaitForElement(text='About')

  def RunSmoothness(self, action_runner):
    interaction = action_runner.BeginGestureInteraction(
        'ScrollAction', is_smooth=True)
    action_runner.ScrollPage()
    interaction.End()


class TwitterPage(Top25Page):

  """ Why: #8 (Alexa global),Picked an interesting page """

  def __init__(self, page_set):
    super(TwitterPage, self).__init__(
        url='https://twitter.com/katyperry',
        page_set=page_set,
        name='Twitter')

  def RunNavigateSteps(self, action_runner):
    action_runner.NavigateToPage(self)
    action_runner.Wait(2)

  def RunSmoothness(self, action_runner):
    interaction = action_runner.BeginGestureInteraction(
        'ScrollAction', is_smooth=True)
    action_runner.ScrollPage()
    interaction.End()


class PinterestPage(Top25Page):

  """ Why: #37 (Alexa global) """

  def __init__(self, page_set):
    super(PinterestPage, self).__init__(
        url='http://pinterest.com',
        page_set=page_set,
        name='Pinterest')

  def RunSmoothness(self, action_runner):
    interaction = action_runner.BeginGestureInteraction(
        'ScrollAction', is_smooth=True)
    action_runner.ScrollPage()
    interaction.End()


class ESPNPage(Top25Page):

  """ Why: #1 sports """

  def __init__(self, page_set):
    super(ESPNPage, self).__init__(
        url='http://espn.go.com',
        page_set=page_set,
        name='ESPN')

  def RunSmoothness(self, action_runner):
    interaction = action_runner.BeginGestureInteraction(
        'ScrollAction', is_smooth=True)
    action_runner.ScrollPage(left_start_ratio=0.1)
    interaction.End()


class YahooGamesPage(Top25Page):

  """ Why: #1 games according to Alexa (with actual games in it) """

  def __init__(self, page_set):
    super(YahooGamesPage, self).__init__(
        url='http://games.yahoo.com',
        page_set=page_set)

  def RunNavigateSteps(self, action_runner):
    action_runner.NavigateToPage(self)
    action_runner.Wait(2)


class Top25PageSet(page_set_module.PageSet):

  """ Pages hand-picked for 2012 CrOS scrolling tuning efforts. """

  def __init__(self):
    super(Top25PageSet, self).__init__(
        user_agent_type='desktop',
        archive_data_file='data/top_25.json',
        bucket=page_set_module.PARTNER_BUCKET)

    self.AddPage(GoogleWebSearchPage(self))
    self.AddPage(GmailPage(self))
    self.AddPage(GoogleCalendarPage(self))
    # Why: tough image case; top google properties
    self.AddPage(
        Top25Page('https://www.google.com/search?q=cats&tbm=isch',
                  page_set=self, credentials='google'))
    self.AddPage(GoogleDocPage(self))
    self.AddPage(GooglePlusPage(self))
    self.AddPage(YoutubePage(self))
    self.AddPage(BlogspotPage(self))
    self.AddPage(WordpressPage(self))
    self.AddPage(FacebookPage(self))
    # Why: #12 (Alexa global), Public profile.
    self.AddPage(
        Top25Page(
            'http://www.linkedin.com/in/linustorvalds', page_set=self,
            name='LinkedIn'))
    # Why: #6 (Alexa) most visited worldwide,Picked an interesting page
    self.AddPage(
        Top25Page(
            'http://en.wikipedia.org/wiki/Wikipedia', page_set=self,
            name='Wikipedia (1 tab)'))
    self.AddPage(TwitterPage(self))
    self.AddPage(PinterestPage(self))
    self.AddPage(ESPNPage(self))
    # Why: #7 (Alexa news); #27 total time spent, picked interesting page.
    self.AddPage(Top25Page(
        url='http://www.weather.com/weather/right-now/Mountain+View+CA+94043',
        page_set=self,
        name='Weather.com'))
    self.AddPage(YahooGamesPage(self))

    other_urls = [
        # Why: #1 news worldwide (Alexa global)
        'http://news.yahoo.com',
        # Why: #2 news worldwide
        'http://www.cnn.com',
        # Why: #1 world commerce website by visits; #3 commerce in the US by
        # time spent
        'http://www.amazon.com',
        # Why: #1 commerce website by time spent by users in US
        'http://www.ebay.com',
        # Why: #1 Alexa recreation
        'http://booking.com',
        # Why: #1 Alexa reference
        'http://answers.yahoo.com',
        # Why: #1 Alexa sports
        'http://sports.yahoo.com/',
        # Why: top tech blog
        'http://techcrunch.com'
    ]

    for url in other_urls:
      self.AddPage(Top25Page(url, self))
