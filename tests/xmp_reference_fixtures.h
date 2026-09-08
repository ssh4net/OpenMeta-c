// SPDX-License-Identifier: Apache-2.0
// XML fixture snapshot from OpenMeta f11de3e0 tests/xmp_decode_test.cc.
// Construction only; the linked pinned decoder supplies expectations.
namespace omc_xmp_reference_fixtures
{
template <class Run> bool run(Run &&run_case)
{
    bool ok = true;
    {
        std::string xmp =
            "<?xpacket begin='\\xEF\\xBB\\xBF' id='W5M0MpCehiHzreSzNTczkc9d'?>"
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:dc='http://purl.org/dc/elements/1.1/' "
            "xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
            "xmlns:xmpMM='http://ns.adobe.com/xap/1.0/mm/' "
            "xmp:CreatorTool='OpenMeta'>"
            "<dc:creator><rdf:Seq>"
            "<rdf:li>John</rdf:li><rdf:li>Jane</rdf:li>"
            "</rdf:Seq></dc:creator>"
            "<xmp:Rating> 5 </xmp:Rating>"
            "<xmpMM:InstanceID rdf:resource='uuid:123'/>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>"
            "<?xpacket end='w'?>";
        ok = run_case("xmp_ref_DecodesAttributesArraysAndRdfResource_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<?xpacket begin='\\xEF\\xBB\\xBF' id='W5M0MpCehiHzreSzNTczkc9d'?>"
            "<xmp:xmpmeta xmlns:xmp='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/'>"
            "<xmp:Rating>0</xmp:Rating>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</xmp:xmpmeta>"
            "<?xpacket end='w'?>";
        xmp.append("\0\0\0padding", 10U);
        ok = run_case("xmp_ref_TrimsXmpMetaCloseWithAlternatePrefix_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description xmlns:xmp='urn:vendor:shadow-xmp:'>"
            "<xmp:Flag>shadow</xmp:Flag>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case("xmp_ref_NamespaceRebindingDoesNotLeakBetweenPackets_shadow_xmp",
                      xmp) &&
             ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/'>"
            "<xmp:CreatorTool>OpenMeta</xmp:CreatorTool>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok =
            run_case("xmp_ref_NamespaceRebindingDoesNotLeakBetweenPackets_standard_xmp",
                     xmp) &&
            ok;
    }
    {
        std::string xmp =
            "<xmp:xmpmeta xmlns:xmp='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "rdf:about='uuid:faf5bdd5-ba3d-11da-ad31-d33d75182f1b' "
            "xmlns:dc='http://purl.org/dc/elements/1.1/'>"
            "<dc:subject><rdf:Bag></rdf:Bag></dc:subject>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</xmp:xmpmeta>";
        ok = run_case("xmp_ref_DecodesRdfAboutAndEmptyBag_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description rdf:about=''/>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case("xmp_ref_SkipsEmptyRdfAbout_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:xmpMM='http://ns.adobe.com/xap/1.0/mm/' "
            "xmlns:stRef='http://ns.adobe.com/xap/1.0/sType/ResourceRef#' "
            "xmlns:stEvt='http://ns.adobe.com/xap/1.0/sType/ResourceEvent#'>"
            "<xmpMM:DerivedFrom rdf:parseType='Resource'>"
            "<stRef:documentID>xmp.did:base</stRef:documentID>"
            "<stRef:instanceID>xmp.iid:base</stRef:instanceID>"
            "<stRef:manageTo>https://example.invalid/base</stRef:manageTo>"
            "</xmpMM:DerivedFrom>"
            "<xmpMM:ManagedFrom rdf:parseType='Resource'>"
            "<stRef:documentID>xmp.did:managed</stRef:documentID>"
            "<stRef:instanceID>xmp.iid:managed</stRef:instanceID>"
            "</xmpMM:ManagedFrom>"
            "<xmpMM:Ingredients><rdf:Bag>"
            "<rdf:li rdf:parseType='Resource'>"
            "<stRef:documentID>xmp.did:ingredient</stRef:documentID>"
            "<stRef:instanceID>xmp.iid:ingredient</stRef:instanceID>"
            "</rdf:li>"
            "</rdf:Bag></xmpMM:Ingredients>"
            "<xmpMM:RenditionOf rdf:parseType='Resource'>"
            "<stRef:documentID>xmp.did:rendition</stRef:documentID>"
            "<stRef:filePath>/tmp/rendition.jpg</stRef:filePath>"
            "<stRef:renditionClass>proof:pdf</stRef:renditionClass>"
            "</xmpMM:RenditionOf>"
            "<xmpMM:History><rdf:Seq>"
            "<rdf:li rdf:parseType='Resource'>"
            "<stEvt:action>saved</stEvt:action>"
            "<stEvt:softwareAgent>OpenMeta</stEvt:softwareAgent>"
            "<stEvt:when>2026-04-15T09:00:00Z</stEvt:when>"
            "</rdf:li>"
            "</rdf:Seq></xmpMM:History>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok =
            run_case("xmp_ref_DecodesXmpMmStructuredMixedNamespaceChildren_xmp", xmp) &&
            ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:xmpMM='http://ns.adobe.com/xap/1.0/mm/' "
            "xmlns:dc='http://purl.org/dc/elements/1.1/' "
            "xmlns:stRef='http://ns.adobe.com/xap/1.0/sType/ResourceRef#' "
            "xmlns:stEvt='http://ns.adobe.com/xap/1.0/sType/ResourceEvent#' "
            "xmlns:stVer='http://ns.adobe.com/xap/1.0/sType/Version#' "
            "xmlns:vendor='https://example.invalid/xmp/vendor/' "
            "xmlns:other='urn:openmeta:other'>"
            "<xmpMM:Pantry><rdf:Bag>"
            "<rdf:li rdf:parseType='Resource'>"
            "<xmpMM:InstanceID>uuid:pantry-1</xmpMM:InstanceID>"
            "<dc:format>image/jpeg</dc:format>"
            "<xmpMM:DerivedFrom rdf:parseType='Resource'>"
            "<stRef:documentID>xmp.did:pantry-source</stRef:documentID>"
            "</xmpMM:DerivedFrom>"
            "<xmpMM:History><rdf:Seq>"
            "<rdf:li rdf:parseType='Resource'>"
            "<stEvt:action>copied</stEvt:action>"
            "</rdf:li>"
            "</rdf:Seq></xmpMM:History>"
            "<xmpMM:Versions><rdf:Seq>"
            "<rdf:li rdf:parseType='Resource'>"
            "<stVer:comments>Embedded component</stVer:comments>"
            "<stVer:event rdf:parseType='Resource'>"
            "<stEvt:softwareAgent>Pantry Writer</stEvt:softwareAgent>"
            "</stVer:event>"
            "</rdf:li>"
            "</rdf:Seq></xmpMM:Versions>"
            "<vendor:Payload>opaque-vendor-data</vendor:Payload>"
            "<other:Payload>other-vendor-data</other:Payload>"
            "</rdf:li>"
            "</rdf:Bag></xmpMM:Pantry>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case("xmp_ref_DecodesXmpMmPantryStructuredChildren_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:xmpMM='http://ns.adobe.com/xap/1.0/mm/'>"
            "<xmpMM:DerivedFrom rdf:parseType='Resource'>"
            "<documentID>xmp.did:base</documentID>"
            "<instanceID>xmp.iid:base</instanceID>"
            "<manageTo>https://example.invalid/base</manageTo>"
            "</xmpMM:DerivedFrom>"
            "<xmpMM:RenditionOf rdf:parseType='Resource'>"
            "<filePath>/tmp/rendition.jpg</filePath>"
            "</xmpMM:RenditionOf>"
            "<xmpMM:Manifest><rdf:Seq>"
            "<rdf:li rdf:parseType='Resource'>"
            "<linkForm>EmbedByReference</linkForm>"
            "<reference rdf:parseType='Resource'>"
            "<filePath>/tmp/manifest.dat</filePath>"
            "<manageUI>https://example.invalid/manage</manageUI>"
            "</reference>"
            "</rdf:li>"
            "</rdf:Seq></xmpMM:Manifest>"
            "<xmpMM:Versions><rdf:Seq>"
            "<rdf:li rdf:parseType='Resource'>"
            "<version>1.0</version>"
            "<event rdf:parseType='Resource'>"
            "<action>saved</action>"
            "<parameters>chapter=1</parameters>"
            "</event>"
            "</rdf:li>"
            "</rdf:Seq></xmpMM:Versions>"
            "<xmpMM:Pantry><rdf:Bag>"
            "<rdf:li rdf:parseType='Resource'>"
            "<InstanceID>uuid:pantry-1</InstanceID>"
            "<format>image/jpeg</format>"
            "</rdf:li>"
            "</rdf:Bag></xmpMM:Pantry>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case("xmp_ref_DecodesLegacyUnqualifiedXmpMmStructuredChildren_xmp",
                      xmp) &&
             ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:xmpBJ='http://ns.adobe.com/xap/1.0/bj/' "
            "xmlns:stJob='http://ns.adobe.com/xap/1.0/sType/Job#' "
            "xmlns:xmpTPg='http://ns.adobe.com/xap/1.0/t/pg/' "
            "xmlns:stDim='http://ns.adobe.com/xap/1.0/sType/Dimensions#' "
            "xmlns:stFnt='http://ns.adobe.com/xap/1.0/sType/Font#' "
            "xmlns:xmpG='http://ns.adobe.com/xap/1.0/g/' "
            "xmlns:xmpDM='http://ns.adobe.com/xmp/1.0/DynamicMedia/'>"
            "<xmpBJ:JobRef><rdf:Bag>"
            "<rdf:li rdf:parseType='Resource'>"
            "<stJob:id>job-1</stJob:id>"
            "<stJob:name>Layout Pass</stJob:name>"
            "<stJob:url>https://example.test/job/1</stJob:url>"
            "</rdf:li>"
            "</rdf:Bag></xmpBJ:JobRef>"
            "<xmpTPg:MaxPageSize rdf:parseType='Resource'>"
            "<stDim:w>8.5</stDim:w>"
            "<stDim:h>11</stDim:h>"
            "<stDim:unit>inch</stDim:unit>"
            "</xmpTPg:MaxPageSize>"
            "<xmpTPg:Fonts><rdf:Bag>"
            "<rdf:li rdf:parseType='Resource'>"
            "<stFnt:fontName>Source Serif</stFnt:fontName>"
            "<stFnt:childFontFiles><rdf:Seq>"
            "<rdf:li>SourceSerif-Regular.otf</rdf:li>"
            "<rdf:li>SourceSerif-It.otf</rdf:li>"
            "</rdf:Seq></stFnt:childFontFiles>"
            "</rdf:li>"
            "</rdf:Bag></xmpTPg:Fonts>"
            "<xmpTPg:Colorants><rdf:Seq>"
            "<rdf:li rdf:parseType='Resource'>"
            "<xmpG:swatchName>Process Cyan</xmpG:swatchName>"
            "<xmpG:mode>CMYK</xmpG:mode>"
            "</rdf:li>"
            "</rdf:Seq></xmpTPg:Colorants>"
            "<xmpTPg:SwatchGroups><rdf:Seq>"
            "<rdf:li rdf:parseType='Resource'>"
            "<xmpG:groupName>Brand Colors</xmpG:groupName>"
            "<xmpG:groupType>1</xmpG:groupType>"
            "<xmpTPg:Colorants><rdf:Seq>"
            "<rdf:li rdf:parseType='Resource'>"
            "<xmpG:swatchName>Accent Orange</xmpG:swatchName>"
            "<xmpG:mode>RGB</xmpG:mode>"
            "</rdf:li>"
            "</rdf:Seq></xmpTPg:Colorants>"
            "</rdf:li>"
            "</rdf:Seq></xmpTPg:SwatchGroups>"
            "<xmpDM:ProjectRef rdf:parseType='Resource'>"
            "<xmpDM:path>/proj/edit.prproj</xmpDM:path>"
            "<xmpDM:type>movie</xmpDM:type>"
            "</xmpDM:ProjectRef>"
            "<xmpDM:altTimecode rdf:parseType='Resource'>"
            "<xmpDM:timeFormat>2997DropTimecode</xmpDM:timeFormat>"
            "<xmpDM:timeValue>00:00:10:12</xmpDM:timeValue>"
            "<xmpDM:value>312</xmpDM:value>"
            "</xmpDM:altTimecode>"
            "<xmpDM:startTimecode rdf:parseType='Resource'>"
            "<xmpDM:timeFormat>2997NonDropTimecode</xmpDM:timeFormat>"
            "<xmpDM:timeValue>01:00:00:00</xmpDM:timeValue>"
            "<xmpDM:value>107892</xmpDM:value>"
            "</xmpDM:startTimecode>"
            "<xmpDM:duration rdf:parseType='Resource'>"
            "<xmpDM:scale>1/48000</xmpDM:scale>"
            "<xmpDM:value>96000</xmpDM:value>"
            "</xmpDM:duration>"
            "<xmpDM:introTime rdf:parseType='Resource'>"
            "<xmpDM:scale>1/1000</xmpDM:scale>"
            "<xmpDM:value>2500</xmpDM:value>"
            "</xmpDM:introTime>"
            "<xmpDM:outCue rdf:parseType='Resource'>"
            "<xmpDM:scale>1/1000</xmpDM:scale>"
            "<xmpDM:value>18000</xmpDM:value>"
            "</xmpDM:outCue>"
            "<xmpDM:relativeTimestamp rdf:parseType='Resource'>"
            "<xmpDM:scale>1/1000</xmpDM:scale>"
            "<xmpDM:value>450</xmpDM:value>"
            "</xmpDM:relativeTimestamp>"
            "<xmpDM:videoFrameSize rdf:parseType='Resource'>"
            "<stDim:w>1920</stDim:w>"
            "<stDim:h>1080</stDim:h>"
            "<stDim:unit>pixel</stDim:unit>"
            "</xmpDM:videoFrameSize>"
            "<xmpDM:videoAlphaPremultipleColor rdf:parseType='Resource'>"
            "<xmpG:mode>RGB</xmpG:mode>"
            "<xmpG:red>255</xmpG:red>"
            "<xmpG:green>0</xmpG:green>"
            "<xmpG:blue>255</xmpG:blue>"
            "</xmpDM:videoAlphaPremultipleColor>"
            "<xmpDM:beatSpliceParams rdf:parseType='Resource'>"
            "<xmpDM:riseInDecibel>3.5</xmpDM:riseInDecibel>"
            "<xmpDM:riseInTimeDuration rdf:parseType='Resource'>"
            "<xmpDM:scale>1/1000</xmpDM:scale>"
            "<xmpDM:value>1200</xmpDM:value>"
            "</xmpDM:riseInTimeDuration>"
            "<xmpDM:useFileBeatsMarker>True</xmpDM:useFileBeatsMarker>"
            "</xmpDM:beatSpliceParams>"
            "<xmpDM:markers rdf:parseType='Resource'>"
            "<xmpDM:name>Verse 1</xmpDM:name>"
            "<xmpDM:startTime>00:00:05.000</xmpDM:startTime>"
            "<xmpDM:cuePointParams rdf:parseType='Resource'>"
            "<xmpDM:key>chapter</xmpDM:key>"
            "<xmpDM:value>intro</xmpDM:value>"
            "</xmpDM:cuePointParams>"
            "</xmpDM:markers>"
            "<xmpDM:resampleParams rdf:parseType='Resource'>"
            "<xmpDM:quality>high</xmpDM:quality>"
            "</xmpDM:resampleParams>"
            "<xmpDM:timeScaleParams rdf:parseType='Resource'>"
            "<xmpDM:frameOverlappingPercentage>12.5</xmpDM:frameOverlappingPercentage>"
            "<xmpDM:frameSize>48</xmpDM:frameSize>"
            "<xmpDM:quality>medium</xmpDM:quality>"
            "</xmpDM:timeScaleParams>"
            "<xmpDM:contributedMedia><rdf:Bag>"
            "<rdf:li rdf:parseType='Resource'>"
            "<xmpDM:path>/media/broll.mov</xmpDM:path>"
            "<xmpDM:managed>True</xmpDM:managed>"
            "<xmpDM:track>V1</xmpDM:track>"
            "<xmpDM:webStatement>https://example.test/media/broll</xmpDM:webStatement>"
            "<xmpDM:duration rdf:parseType='Resource'>"
            "<xmpDM:scale>1/24000</xmpDM:scale>"
            "<xmpDM:value>48000</xmpDM:value>"
            "</xmpDM:duration>"
            "<xmpDM:startTime rdf:parseType='Resource'>"
            "<xmpDM:scale>1/24000</xmpDM:scale>"
            "<xmpDM:value>1200</xmpDM:value>"
            "</xmpDM:startTime>"
            "</rdf:li>"
            "</rdf:Bag></xmpDM:contributedMedia>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok =
            run_case("xmp_ref_DecodesAdobeStructuredWorkflowNamespaces_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:xmpDM='http://ns.adobe.com/xmp/1.0/DynamicMedia/'>"
            "<xmpDM:Tracks><rdf:Bag>"
            "<rdf:li rdf:parseType='Resource'>"
            "<xmpDM:trackName>Dialogue</xmpDM:trackName>"
            "<xmpDM:trackType>Audio</xmpDM:trackType>"
            "<xmpDM:frameRate>f24000</xmpDM:frameRate>"
            "<xmpDM:markers rdf:parseType='Resource'>"
            "<xmpDM:name>Scene 1</xmpDM:name>"
            "<xmpDM:startTime>00:00:01.000</xmpDM:startTime>"
            "<xmpDM:cuePointParams rdf:parseType='Resource'>"
            "<xmpDM:key>chapter</xmpDM:key>"
            "<xmpDM:value>scene-1</xmpDM:value>"
            "</xmpDM:cuePointParams>"
            "</xmpDM:markers>"
            "</rdf:li>"
            "</rdf:Bag></xmpDM:Tracks>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case("xmp_ref_DecodesXmpDmTracksStructuredChildren_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:xmpMM='http://ns.adobe.com/xap/1.0/mm/' "
            "xmlns:stMfs='http://ns.adobe.com/xap/1.0/sType/ManifestItem#' "
            "xmlns:stRef='http://ns.adobe.com/xap/1.0/sType/ResourceRef#'>"
            "<xmpMM:Manifest><rdf:Seq>"
            "<rdf:li rdf:parseType='Resource'>"
            "<stMfs:linkForm>EmbedByReference</stMfs:linkForm>"
            "<stMfs:reference rdf:parseType='Resource'>"
            "<stRef:filePath>C:\\some path\\file.ext</stRef:filePath>"
            "</stMfs:reference>"
            "</rdf:li>"
            "</rdf:Seq></xmpMM:Manifest>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case("xmp_ref_DecodesXmpMmManifestStructuredChildren_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:xmpMM='http://ns.adobe.com/xap/1.0/mm/' "
            "xmlns:stVer='http://ns.adobe.com/xap/1.0/sType/Version#' "
            "xmlns:stEvt='http://ns.adobe.com/xap/1.0/sType/ResourceEvent#'>"
            "<xmpMM:Versions><rdf:Seq>"
            "<rdf:li rdf:parseType='Resource'>"
            "<stVer:version>1.0</stVer:version>"
            "<stVer:comments>Initial import</stVer:comments>"
            "<stVer:modifier>OpenMeta</stVer:modifier>"
            "<stVer:modifyDate>2026-04-16T10:15:00Z</stVer:modifyDate>"
            "<stVer:event rdf:parseType='Resource'>"
            "<stEvt:action>saved</stEvt:action>"
            "<stEvt:changed>/metadata</stEvt:changed>"
            "<stEvt:when>2026-04-16T10:15:00Z</stEvt:when>"
            "</stVer:event>"
            "</rdf:li>"
            "</rdf:Seq></xmpMM:Versions>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case("xmp_ref_DecodesXmpMmVersionsStructuredChildren_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/'>"
            "<Iptc4xmpExt:ImageRegion><rdf:Bag>"
            "<rdf:li rdf:parseType='Resource'>"
            "<Iptc4xmpExt:RegionBoundary rdf:parseType='Resource'>"
            "<Iptc4xmpExt:rbShape>rectangle</Iptc4xmpExt:rbShape>"
            "<Iptc4xmpExt:rbUnit>pixel</Iptc4xmpExt:rbUnit>"
            "<Iptc4xmpExt:rbX>10</Iptc4xmpExt:rbX>"
            "<Iptc4xmpExt:rbY>20</Iptc4xmpExt:rbY>"
            "<Iptc4xmpExt:rbW>300</Iptc4xmpExt:rbW>"
            "<Iptc4xmpExt:rbH>200</Iptc4xmpExt:rbH>"
            "</Iptc4xmpExt:RegionBoundary>"
            "</rdf:li></rdf:Bag></Iptc4xmpExt:ImageRegion>"
            "</rdf:Description></rdf:RDF></x:xmpmeta>";
        ok = run_case("xmp_ref_ResolvesIptcImageRegionBoundaryGeometry_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description xmlns:dc='http://purl.org/dc/elements/1.1/'>"
            "<dc:title><rdf:Alt>"
            "<rdf:li xml:lang='x-default'>Default title</rdf:li>"
            "<rdf:li xml:lang='fr-FR'>Titre</rdf:li>"
            "</rdf:Alt></dc:title>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case("xmp_ref_DecodesAltTextEntriesWithXmlLangPaths_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:Iptc4xmpCore='http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/'>"
            "<Iptc4xmpCore:CreatorContactInfo rdf:parseType='Resource'>"
            "<Iptc4xmpCore:CiEmailWork>editor@example.test</Iptc4xmpCore:CiEmailWork>"
            "<Iptc4xmpCore:CiUrlWork>https://example.test/contact</"
            "Iptc4xmpCore:CiUrlWork>"
            "</Iptc4xmpCore:CreatorContactInfo>"
            "<Iptc4xmpCore:LocationCreated rdf:parseType='Resource'>"
            "<Iptc4xmpCore:City>Paris</Iptc4xmpCore:City>"
            "<Iptc4xmpCore:CountryName>France</Iptc4xmpCore:CountryName>"
            "</Iptc4xmpCore:LocationCreated>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case("xmp_ref_DecodesStructuredResourcePaths_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:plus='http://ns.useplus.org/ldf/xmp/1.0/'>"
            "<plus:Licensee><rdf:Seq>"
            "<rdf:li rdf:parseType='Resource'>"
            "<plus:LicenseeName>Example Archive</plus:LicenseeName>"
            "<plus:LicenseeURL>https://example.test/archive</plus:LicenseeURL>"
            "</rdf:li>"
            "<rdf:li rdf:parseType='Resource'>"
            "<plus:LicenseeName>Editorial Partner</plus:LicenseeName>"
            "<plus:LicenseeID>lic-002</plus:LicenseeID>"
            "</rdf:li>"
            "</rdf:Seq></plus:Licensee>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case("xmp_ref_DecodesIndexedStructuredResourcePaths_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/'>"
            "<Iptc4xmpExt:LocationShown><rdf:Seq>"
            "<rdf:li rdf:parseType='Resource'>"
            "<Iptc4xmpExt:City>Paris</Iptc4xmpExt:City>"
            "<Iptc4xmpExt:CountryName>France</Iptc4xmpExt:CountryName>"
            "</rdf:li>"
            "<rdf:li rdf:parseType='Resource'>"
            "<Iptc4xmpExt:City>Kyoto</Iptc4xmpExt:City>"
            "<Iptc4xmpExt:CountryName>Japan</Iptc4xmpExt:CountryName>"
            "</rdf:li>"
            "</rdf:Seq></Iptc4xmpExt:LocationShown>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok =
            run_case("xmp_ref_DecodesIptc4xmpExtIndexedStructuredPaths_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:Iptc4xmpCore='http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/'>"
            "<Iptc4xmpCore:CreatorContactInfo rdf:parseType='Resource'>"
            "<Iptc4xmpCore:CiEmailWork>editor@example.test</Iptc4xmpCore:CiEmailWork>"
            "<Iptc4xmpCore:CiAdrCity><rdf:Alt>"
            "<rdf:li xml:lang='x-default'>Tokyo</rdf:li>"
            "<rdf:li xml:lang='ja-JP'>東京</rdf:li>"
            "</rdf:Alt></Iptc4xmpCore:CiAdrCity>"
            "</Iptc4xmpCore:CreatorContactInfo>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case("xmp_ref_DecodesStructuredChildLangAltPaths_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/'>"
            "<Iptc4xmpExt:LocationShown><rdf:Seq>"
            "<rdf:li rdf:parseType='Resource'>"
            "<Iptc4xmpExt:CountryName>Japan</Iptc4xmpExt:CountryName>"
            "<Iptc4xmpExt:Sublocation><rdf:Alt>"
            "<rdf:li xml:lang='x-default'>Gion</rdf:li>"
            "<rdf:li xml:lang='ja-JP'>祇園</rdf:li>"
            "</rdf:Alt></Iptc4xmpExt:Sublocation>"
            "</rdf:li>"
            "</rdf:Seq></Iptc4xmpExt:LocationShown>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case("xmp_ref_DecodesIndexedStructuredChildLangAltPaths_xmp", xmp) &&
             ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:Iptc4xmpCore='http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/'>"
            "<Iptc4xmpCore:CreatorContactInfo rdf:parseType='Resource'>"
            "<Iptc4xmpCore:CiAdrExtadr><rdf:Seq>"
            "<rdf:li>Line 1</rdf:li>"
            "<rdf:li>Line 2</rdf:li>"
            "</rdf:Seq></Iptc4xmpCore:CiAdrExtadr>"
            "</Iptc4xmpCore:CreatorContactInfo>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case("xmp_ref_DecodesStructuredChildIndexedPaths_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/'>"
            "<Iptc4xmpExt:LocationShown><rdf:Seq>"
            "<rdf:li rdf:parseType='Resource'>"
            "<Iptc4xmpExt:Sublocation><rdf:Seq>"
            "<rdf:li>Gion</rdf:li>"
            "<rdf:li>Hanamikoji</rdf:li>"
            "</rdf:Seq></Iptc4xmpExt:Sublocation>"
            "</rdf:li>"
            "</rdf:Seq></Iptc4xmpExt:LocationShown>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case("xmp_ref_DecodesIndexedStructuredChildIndexedPaths_xmp", xmp) &&
             ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:Iptc4xmpCore='http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/'>"
            "<Iptc4xmpCore:CreatorContactInfo rdf:parseType='Resource'>"
            "<Iptc4xmpCore:CiAdrRegion rdf:parseType='Resource'>"
            "<Iptc4xmpCore:ProvinceName>Tokyo</Iptc4xmpCore:ProvinceName>"
            "<Iptc4xmpCore:ProvinceCode>13</Iptc4xmpCore:ProvinceCode>"
            "</Iptc4xmpCore:CiAdrRegion>"
            "</Iptc4xmpCore:CreatorContactInfo>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case("xmp_ref_DecodesNestedStructuredResourcePaths_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/'>"
            "<Iptc4xmpExt:LocationShown><rdf:Seq>"
            "<rdf:li rdf:parseType='Resource'>"
            "<Iptc4xmpExt:Address rdf:parseType='Resource'>"
            "<Iptc4xmpExt:City>Kyoto</Iptc4xmpExt:City>"
            "<Iptc4xmpExt:CountryName>Japan</Iptc4xmpExt:CountryName>"
            "</Iptc4xmpExt:Address>"
            "</rdf:li>"
            "</rdf:Seq></Iptc4xmpExt:LocationShown>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case("xmp_ref_DecodesIndexedNestedStructuredResourcePaths_xmp", xmp) &&
             ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:Iptc4xmpCore='http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/'>"
            "<Iptc4xmpCore:CreatorContactInfo rdf:parseType='Resource'>"
            "<Iptc4xmpCore:CiAdrRegion rdf:parseType='Resource'>"
            "<Iptc4xmpCore:ProvinceName><rdf:Alt>"
            "<rdf:li xml:lang='x-default'>Tokyo</rdf:li>"
            "<rdf:li xml:lang='ja-JP'>東京</rdf:li>"
            "</rdf:Alt></Iptc4xmpCore:ProvinceName>"
            "</Iptc4xmpCore:CiAdrRegion>"
            "</Iptc4xmpCore:CreatorContactInfo>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok =
            run_case("xmp_ref_DecodesNestedStructuredChildLangAltPaths_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:Iptc4xmpCore='http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/'>"
            "<Iptc4xmpCore:CreatorContactInfo rdf:parseType='Resource'>"
            "<Iptc4xmpCore:CiAdrRegion rdf:parseType='Resource'>"
            "<Iptc4xmpCore:ProvinceCode><rdf:Seq>"
            "<rdf:li>13</rdf:li>"
            "<rdf:li>JP-13</rdf:li>"
            "</rdf:Seq></Iptc4xmpCore:ProvinceCode>"
            "</Iptc4xmpCore:CiAdrRegion>"
            "</Iptc4xmpCore:CreatorContactInfo>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok =
            run_case("xmp_ref_DecodesNestedStructuredChildIndexedPaths_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/'>"
            "<Iptc4xmpExt:LocationShown><rdf:Seq>"
            "<rdf:li rdf:parseType='Resource'>"
            "<Iptc4xmpExt:Address rdf:parseType='Resource'>"
            "<Iptc4xmpExt:City><rdf:Alt>"
            "<rdf:li xml:lang='x-default'>Kyoto</rdf:li>"
            "<rdf:li xml:lang='ja-JP'>京都</rdf:li>"
            "</rdf:Alt></Iptc4xmpExt:City>"
            "</Iptc4xmpExt:Address>"
            "</rdf:li>"
            "</rdf:Seq></Iptc4xmpExt:LocationShown>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case("xmp_ref_DecodesIndexedNestedStructuredChildLangAltPaths_xmp",
                      xmp) &&
             ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/'>"
            "<Iptc4xmpExt:LocationShown><rdf:Seq>"
            "<rdf:li rdf:parseType='Resource'>"
            "<Iptc4xmpExt:Address rdf:parseType='Resource'>"
            "<Iptc4xmpExt:CountryCode><rdf:Seq>"
            "<rdf:li>JP</rdf:li>"
            "<rdf:li>JP-26</rdf:li>"
            "</rdf:Seq></Iptc4xmpExt:CountryCode>"
            "</Iptc4xmpExt:Address>"
            "</rdf:li>"
            "</rdf:Seq></Iptc4xmpExt:LocationShown>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case("xmp_ref_DecodesIndexedNestedStructuredChildIndexedPaths_xmp",
                      xmp) &&
             ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
            "xmp:CreatorTool='OpenMeta'/>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        xmp.append(16U, '\0');
        ok = run_case("xmp_ref_TrimsTrailingNulPadding_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/' "
            "xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
            "xmlns:exif='http://ns.adobe.com/exif/1.0/'>"
            "<Iptc4xmpExt:LocationShown><rdf:Seq>"
            "<rdf:li rdf:parseType='Resource'>"
            "<xmp:Identifier><rdf:Bag>"
            "<rdf:li>loc-001</rdf:li>"
            "<rdf:li>loc-002</rdf:li>"
            "</rdf:Bag></xmp:Identifier>"
            "<exif:GPSLatitude>41,24.5N</exif:GPSLatitude>"
            "<exif:GPSLongitude>2,9E</exif:GPSLongitude>"
            "</rdf:li>"
            "</rdf:Seq></Iptc4xmpExt:LocationShown>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case(
                 "xmp_ref_DecodesMixedNamespaceStructuredLocationDetailsChildren_xmp",
                 xmp) &&
             ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/'>"
            "<Iptc4xmpExt:LocationShown><rdf:Seq>"
            "<rdf:li rdf:parseType='Resource'>"
            "<Identifier><rdf:Bag>"
            "<rdf:li>loc-001</rdf:li>"
            "<rdf:li>loc-002</rdf:li>"
            "</rdf:Bag></Identifier>"
            "<GPSLatitude>41,24.5N</GPSLatitude>"
            "<GPSLongitude>2,9E</GPSLongitude>"
            "<GPSAltitude>35.5</GPSAltitude>"
            "<GPSAltitudeRef>Above Sea Level</GPSAltitudeRef>"
            "</rdf:li>"
            "</rdf:Seq></Iptc4xmpExt:LocationShown>"
            "<Iptc4xmpExt:LocationCreated rdf:parseType='Resource'>"
            "<Identifier><rdf:Bag><rdf:li>par-001</rdf:li></rdf:Bag></Identifier>"
            "<GPSLatitude>48,51.507N</GPSLatitude>"
            "</Iptc4xmpExt:LocationCreated>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case(
                 "xmp_ref_"
                 "DecodesLegacyUnqualifiedMixedNamespaceLocationDetailsChildren_xmp",
                 xmp) &&
             ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
            "xmp:CreatorTool='OpenMeta'>"
            "<xmp:Rating>5</xmp:Rating>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case("xmp_ref_EstimateMatchesDecodeCounters_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:tiff='http://ns.adobe.com/tiff/1.0/'>"
            "<tiff:Artist/>"
            "<tiff:Copyright>   </tiff:Copyright>"
            "</rdf:Description>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        ok = run_case("xmp_ref_PreservesExplicitEmptyLeafValues_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
            "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
            "<rdf:Description "
            "xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
            "xmp:CreatorTool='OpenMeta'/>"
            "</rdf:RDF>"
            "</x:xmpmeta>";
        xmp = std::string("application/rdf+xml\0", 20U) + xmp;
        xmp.append(8U, '\0');
        ok = run_case("xmp_ref_SkipsLeadingMimePrefix_xmp", xmp) && ok;
    }
    {
        std::string xmp =
            "<?xpacket begin='' id='W5M0MpCehiHzreSzNTczkc9d' ?>"
            "<x:xmpmeta xmlns:x='adobe:ns:meta/' "
            "x:xmptk='Adobe XMP Core 5.2-c004 1.136881, 2010/06/10-18:11:35'>"
            "</x:xmpmeta>"
            "<?xpacket end='w'?>";
        ok = run_case("xmp_ref_DecodesXmpToolkitOnXmpMetaRoot_xmp", xmp) && ok;
    }
    return ok;
}
} // namespace omc_xmp_reference_fixtures
