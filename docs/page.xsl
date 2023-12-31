<?xml version="1.0"?>
<xsl:stylesheet
  xmlns="http://www.w3.org/1999/xhtml"
  xmlns:html="http://www.w3.org/1999/xhtml"
  xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:exsl="http://exslt.org/common"
  exclude-result-prefixes="xsl exsl html"
  version="1.0">

  <xsl:param name="builddir" select="'..'"/>

  <xsl:template match="node() | @*" mode="content">
    <xsl:copy>
      <xsl:apply-templates select="node() | @*" mode="content"/>
    </xsl:copy>
  </xsl:template>


  <xsl:template match="html:ul[@id='toc']" mode="content">
    <xsl:call-template name="toc"/>
  </xsl:template>

  <xsl:template match="html:div[@id='include']" mode="content">
    <xsl:call-template name="include"/>
  </xsl:template>

  <xsl:template name="toc">
    <ul>
      <xsl:for-each select="/html:html/html:body/html:h2[count(html:a) = 1]">
        <xsl:variable name="thish2" select="."/>
        <li>
          <a href="#{html:a/@id}"><xsl:value-of select="html:a/text()"/></a>
          <xsl:if test="count(./following-sibling::html:h3[preceding-sibling::html:h2[1] = $thish2 and count(html:a) = 1]) > 0">
            <ul>
              <xsl:for-each select="./following-sibling::html:h3[preceding-sibling::html:h2[1] = $thish2 and count(html:a) = 1]">
                <xsl:variable name="thish3" select="."/>
                <li>
                  <a href="#{html:a/@id}"><xsl:value-of select="html:a/text()"/></a>
                  <xsl:if test="count(./following-sibling::html:h4[preceding-sibling::html:h3[1] = $thish3 and count(html:a) = 1]) > 0">
                    <ul>
                      <xsl:for-each select="./following-sibling::html:h4[preceding-sibling::html:h3[1] = $thish3 and count(html:a) = 1]">
                        <xsl:variable name="thish4" select="."/>
                        <li>
                          <a href="#{html:a/@id}"><xsl:value-of select="html:a/text()"/></a>
                          <xsl:if test="count(./following-sibling::html:h5[preceding-sibling::html:h4[1] = $thish4 and count(html:a) = 1]) > 0">
                            <ul>
                              <xsl:for-each select="./following-sibling::html:h5[preceding-sibling::html:h4[1] = $thish4 and count(html:a) = 1]">
                                <xsl:variable name="thish5" select="."/>
                                <li>
                                  <a href="#{html:a/@id}"><xsl:value-of select="html:a/text()"/></a>
                                  <xsl:if test="count(./following-sibling::html:h6[preceding-sibling::html:h5[1] = $thish5 and count(html:a) = 1]) > 0">
                                    <ul>
                                      <xsl:for-each select="./following-sibling::html:h6[preceding-sibling::html:h5[1] = $thish5 and count(html:a) = 1]">
                                        <li>
                                          <a href="#{html:a/@id}"><xsl:value-of select="html:a/text()"/></a>
                                        </li>
                                      </xsl:for-each>
                                    </ul>
                                  </xsl:if>
                                </li>
                              </xsl:for-each>
                            </ul>
                          </xsl:if>
                        </li>
                      </xsl:for-each>
                    </ul>
                  </xsl:if>
                </li>
              </xsl:for-each>
            </ul>
          </xsl:if>
        </li>
      </xsl:for-each>
    </ul>
  </xsl:template>

  <!-- This is the master page structure -->
  <xsl:template match="/" mode="page">
    <xsl:param name="pagename"/>
    <xsl:param name="timestamp"/>
    <xsl:text disable-output-escaping="yes">&lt;!DOCTYPE html&gt;
</xsl:text>
    <html>
      <xsl:comment>
        This file is autogenerated from <xsl:value-of select="$pagename"/>.in
        Do not edit this file. Changes will be lost.
      </xsl:comment>
      <xsl:comment>
        This page was generated at <xsl:value-of select="$timestamp"/>.
      </xsl:comment>
      <head>
        <meta charset="UTF-8"/>
        <meta name="viewport" content="width=device-width, initial-scale=1"/>
        <link rel="stylesheet" type="text/css" href="{$href_base}main.css"/>
        <link rel="apple-touch-icon" sizes="180x180" href="/apple-touch-icon.png"/>
        <link rel="icon" type="image/png" sizes="32x32" href="/favicon-32x32.png"/>
        <link rel="icon" type="image/png" sizes="16x16" href="/favicon-16x16.png"/>
        <link rel="manifest" href="/manifest.json"/>
        <meta name="theme-color" content="#ffffff"/>
        <title>libvirt: <xsl:value-of select="html:html/html:body//html:h1"/></title>
        <meta name="description" content="libvirt, virtualization, virtualization API"/>
        <xsl:if test="$pagename = 'libvirt-go.html'">
          <meta name="go-import" content="libvirt.org/libvirt-go git https://libvirt.org/git/libvirt-go.git"/>
        </xsl:if>
        <xsl:if test="$pagename = 'libvirt-go-xml.html'">
          <meta name="go-import" content="libvirt.org/libvirt-go-xml git https://libvirt.org/git/libvirt-go-xml.git"/>
        </xsl:if>
        <xsl:apply-templates select="/html:html/html:head/html:script" mode="content"/>

        <script type="text/javascript" src="{$href_base}js/main.js">
          <xsl:comment>// forces non-empty element</xsl:comment>
        </script>
      </head>
      <body onload="pageload()">
        <xsl:if test="html:html/html:body/@class">
          <xsl:attribute name="class">
            <xsl:value-of select="html:html/html:body/@class"/>
          </xsl:attribute>
        </xsl:if>
        <div id="body">
          <div id="content">
            <xsl:apply-templates select="/html:html/html:body/*" mode="content"/>
          </div>
        </div>
        <div id="nav">
          <div id="home">
            <a href="{$href_base}index.html">Home</a>
          </div>
          <div id="jumplinks">
            <ul>
              <li><a href="{$href_base}downloads.html">Download</a></li>
              <li><a href="{$href_base}contribute.html">Contribute</a></li>
              <li><a href="{$href_base}docs.html">Docs</a></li>
            </ul>
          </div>
          <div id="search">
            <form id="simplesearch" action="https://www.google.com/search" enctype="application/x-www-form-urlencoded" method="get">
              <div>
                <input id="searchsite" name="sitesearch" type="hidden" value="libvirt.org"/>
                <input id="searchq" name="q" type="text" size="12" value=""/>
                <input name="submit" type="submit" value="Go"/>
              </div>
            </form>
            <div id="advancedsearch">
              <span><input type="radio" name="what" id="whatwebsite" checked="checked" value="website"/><label for="whatwebsite">Website</label></span>
              <span><input type="radio" name="what" id="whatwiki" value="wiki"/><label for="whatwiki">Wiki</label></span>
              <span><input type="radio" name="what" id="whatdevs" value="devs"/><label for="whatdevs">Developers list</label></span>
              <span><input type="radio" name="what" id="whatusers" value="users"/><label for="whatusers">Users list</label></span>
            </div>
          </div>
        </div>
        <div id="footer">
          <div id="contact">
            <h3>Contact</h3>
            <ul>
              <li><a href="{$href_base}contact.html#email">email</a></li>
              <li><a href="{$href_base}contact.html#irc">irc</a></li>
            </ul>
          </div>
          <div id="community">
            <h3>Community</h3>
            <ul>
              <li><a href="https://twitter.com/hashtag/libvirt">twitter</a></li>
              <li><a href="http://stackoverflow.com/questions/tagged/libvirt">stackoverflow</a></li>
              <li><a href="http://serverfault.com/questions/tagged/libvirt">serverfault</a></li>
            </ul>
          </div>
          <div id="conduct">
            Participants in the libvirt project agree to abide by <a href="{$href_base}governance.html#codeofconduct">the project code of conduct</a>
          </div>
          <br class="clear"/>
        </div>
      </body>
    </html>
  </xsl:template>

  <xsl:template name="include">
    <xsl:variable name="inchtml">
      <xsl:copy-of select="document(concat($builddir, '/docs/', @filename))"/>
    </xsl:variable>

    <xsl:apply-templates select="exsl:node-set($inchtml)/html:html/html:body/*" mode="content"/>
  </xsl:template>

  <xsl:template match="html:h1 | html:h2 | html:h3 | html:h4 | html:h5 | html:h6" mode="content">
    <xsl:element name="{name()}">
      <xsl:apply-templates mode="copy" />
      <xsl:if test="./html:a/@id">
        <a class="headerlink" href="#{html:a/@id}" title="Permalink to this headline">&#xb6;</a>
      </xsl:if>
      <xsl:if test="./html:a[@class='toc-backref']">
        <a class="headerlink" href="#{../@id}" title="Permalink to this headline">&#xb6;</a>
      </xsl:if>
    </xsl:element>
  </xsl:template>

  <xsl:template match="text()" mode="copy" priority="0">
    <xsl:value-of select="."/>
  </xsl:template>

  <xsl:template match="*" mode="copy">
    <xsl:element name="{name()}">
      <xsl:copy-of select="./@*"/>
      <xsl:apply-templates mode="copy" />
    </xsl:element>
  </xsl:template>
</xsl:stylesheet>
