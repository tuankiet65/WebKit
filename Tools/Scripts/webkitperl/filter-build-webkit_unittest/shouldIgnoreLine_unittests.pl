#!/usr/bin/env perl
#
# Copyright (C) 2013 Apple Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1.  Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
# ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# This script tests parts of filter-build-webkit.
# This script runs the unittests specified in @testFiles.

use strict;
use warnings;

use English;
use FindBin;
use Test::More;
use lib File::Spec->catdir($FindBin::Bin, "..");

if ($^O eq 'MSWin32') {
    plan skip_all => 'filter-build-webkit fails to load on Windows.';
    exit 0;    
}

require "filter-build-webkit";

FilterBuildWebKit->import(qw(shouldIgnoreLine));

sub description($);

#
# Test whitespace
#
is(shouldIgnoreLine("", ""), 1, "Ignored: empty line");
is(shouldIgnoreLine("", " "), 1, "Ignored: one space");
is(shouldIgnoreLine("", "\t"), 1, "Ignored: one tab");

#
# Test input that should be ignored regardless of previous line
#
my @expectIgnoredLines = split(/$INPUT_RECORD_SEPARATOR/, <<'END');
make: Nothing to be done for `all'.
JavaScriptCore/create_hash_table JavaScriptCore/runtime/ArrayConstructor.cpp -i > ArrayConstructor.lut.h
Creating hashtable for JavaScriptCore/runtime/ArrayConstructor.cpp
Wrote output to /Volumes/Data/Build/Release/DerivedSources/WebCore/ExportFileGenerator.cpp
/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/libtool: file: /Volumes/Data/Build/ANGLE.build/Release/ANGLE.build/Objects-normal/i386/debug.o has no symbols
Showing first 200 notices only
printf "WebCore/Modules/encryptedmedia/MediaKeyMessageEvent.idl\nWebCore/Modules/encryptedmedia/MediaKeyNeededEvent.idl\nWebCore/Modules/encryptedmedia/MediaKeySession.idl\nWebCore/Modules/encryptedmedia/MediaKeys.idl\nWebCore/Modules/filesystem/DOMFileSystem.idl\nWebCore/Modules/filesystem/DOMFileSystemSync.idl\nWebCore/Modules/filesystem/DOMWindowFileSystem.idl\nWebCore/Modules/filesystem/DirectoryEntry.idl\nWebCore/Modules/filesystem/DirectoryEntrySync.idl\nWebCore/Modules/filesystem/DirectoryReader.idl\nWebCore/Modules/filesystem/DirectoryReaderSync.idl\nWebCore/Modules/filesystem/EntriesCallback.idl\nWebCore/Modules/filesystem/Entry.idl\nWebCore/Modules/filesystem/EntryArray.idl\nWebCore/Modules/filesystem/EntryArraySync.idl\nWebCore/Modules/filesystem/EntryCallback.idl\nWebCore/Modules/filesystem/EntrySync.idl\nWebCore/Modules/filesystem/ErrorCallback.idl\nWebCore/Modules/filesystem/FileCallback.idl\nWebCore/Modules/filesystem/FileEntry.idl\nWebCore/Modules/filesystem/FileEntrySync.idl\nWebCore/Modules/filesystem/FileSystemCallback.idl\nWebCore/Modules/filesystem/FileWriter.idl\nWebCore/Modules/filesystem/FileWriterCallback.idl\nWebCore/Modules/filesystem/FileWriterSync.idl\nWebCore/Modules/filesystem/Metadata.idl\nWebCore/Modules/filesystem/MetadataCallback.idl\nWebCore/Modules/filesystem/WorkerContextFileSystem.idl\nWebCore/Modules/geolocation/Coordinates.idl\nWebCore/Modules/geolocation/Geolocation.idl\nWebCore/Modules/geolocation/Geoposition.idl\nWebCore/Modules/geolocation/NavigatorGeolocation.idl\nWebCore/Modules/geolocation/PositionCallback.idl\nWebCore/Modules/geolocation/PositionError.idl\nWebCore/Modules/geolocation/PositionErrorCallback.idl\nWebCore/Modules/indexeddb/DOMWindowIndexedDatabase.idl\nWebCore/Modules/indexeddb/IDBAny.idl\nWebCore/Modules/indexeddb/IDBCursor.idl\nWebCore/Modules/indexeddb/IDBDatabase.idl\nWebCore/Modules/indexeddb/IDBFactory.idl\nWebCore/Modules/indexeddb/IDBIndex.idl\nWebCore/Modules/indexeddb/IDBKeyRange.idl\nWebCore/Modules/indexeddb/IDBObjectStore.idl\nWebCore/Modules/indexeddb/IDBRequest.idl\nWebCore/Modules/indexeddb/IDBTransaction.idl\nWebCore/Modules/indexeddb/IDBVersionChangeEvent.idl\nWebCore/Modules/indexeddb/WorkerContextIndexedDatabase.idl\nWebCore/Modules/mediasource/MediaSource.idl\nWebCore/Modules/mediasource/SourceBuffer.idl\nWebCore/Modules/mediasource/SourceBufferList.idl\nWebCore/Modules/notifications/DOMWindowNotifications.idl\nWebCore/Modules/notifications/Notification.idl\nWebCore/Modules/notifications/NotificationCenter.idl\nWebCore/Modules/notifications/NotificationPermissionCallback.idl\nWebCore/Modules/notifications/WorkerContextNotifications.idl\nWebCore/Modules/quota/DOMWindowQuota.idl\nWebCore/Modules/quota/NavigatorStorageQuota.idl\nWebCore/Modules/quota/StorageInfo.idl\nWebCore/Modules/quota/StorageErrorCallback.idl\nWebCore/Modules/quota/StorageQuota.idl\nWebCore/Modules/quota/StorageQuotaCallback.idl\nWebCore/Modules/quota/StorageUsageCallback.idl\nWebCore/Modules/quota/WorkerNavigatorStorageQuota.idl\nWebCore/Modules/speech/DOMWindowSpeechSynthesis.idl\nWebCore/Modules/speech/SpeechSynthesis.idl\nWebCore/Modules/speech/SpeechSynthesisEvent.idl\nWebCore/Modules/speech/SpeechSynthesisUtterance.idl\nWebCore/Modules/speech/SpeechSynthesisVoice.idl\nWebCore/Modules/webaudio/AudioBuffer.idl\nWebCore/Modules/webaudio/AudioBufferCallback.idl\nWebCore/Modules/webaudio/AudioBufferSourceNode.idl\nWebCore/Modules/webaudio/ChannelMergerNode.idl\nWebCore/Modules/webaudio/ChannelSplitterNode.idl\nWebCore/Modules/webaudio/AudioContext.idl\nWebCore/Modules/webaudio/AudioDestinationNode.idl\nWebCore/Modules/webaudio/GainNode.idl\nWebCore/Modules/webaudio/AudioListener.idl\nWebCore/Modules/webaudio/AudioNode.idl\nWebCore/Modules/webaudio/PannerNode.idl\nWebCore/Modules/webaudio/AudioParam.idl\nWebCore/Modules/webaudio/AudioProcessingEvent.idl\nWebCore/Modules/webaudio/BiquadFilterNode.idl\nWebCore/Modules/webaudio/ConvolverNode.idl\nWebCore/Modules/webaudio/DOMWindowWebAudio.idl\nWebCore/Modules/webaudio/DelayNode.idl\nWebCore/Modules/webaudio/DynamicsCompressorNode.idl\nWebCore/Modules/webaudio/ScriptProcessorNode.idl\nWebCore/Modules/webaudio/MediaElementAudioSourceNode.idl\nWebCore/Modules/webaudio/MediaStreamAudioSourceNode.idl\nWebCore/Modules/webaudio/OscillatorNode.idl\nWebCore/Modules/webaudio/OfflineAudioContext.idl\nWebCore/Modules/webaudio/OfflineAudioCompletionEvent.idl\nWebCore/Modules/webaudio/AnalyserNode.idl\nWebCore/Modules/webaudio/WaveShaperNode.idl\nWebCore/Modules/webaudio/WaveTable.idl\nWebCore/Modules/webdatabase/DOMWindowWebDatabase.idl\nWebCore/Modules/webdatabase/Database.idl\nWebCore/Modules/webdatabase/DatabaseCallback.idl\nWebCore/Modules/webdatabase/DatabaseSync.idl\nWebCore/Modules/webdatabase/SQLError.idl\nWebCore/Modules/webdatabase/SQLException.idl\nWebCore/Modules/webdatabase/SQLResultSet.idl\nWebCore/Modules/webdatabase/SQLResultSetRowList.idl\nWebCore/Modules/webdatabase/SQLStatementCallback.idl\nWebCore/Modules/webdatabase/SQLStatementErrorCallback.idl\nWebCore/Modules/webdatabase/SQLTransaction.idl\nWebCore/Modules/webdatabase/SQLTransactionCallback.idl\nWebCore/Modules/webdatabase/SQLTransactionErrorCallback.idl\nWebCore/Modules/webdatabase/SQLTransactionSync.idl\nWebCore/Modules/webdatabase/SQLTransactionSyncCallback.idl\nWebCore/Modules/webdatabase/WorkerContextWebDatabase.idl\nWebCore/Modules/websockets/CloseEvent.idl\nWebCore/Modules/websockets/DOMWindowWebSocket.idl\nWebCore/Modules/websockets/WebSocket.idl\nWebCore/Modules/websockets/WorkerContextWebSocket.idl\nWebCore/css/CSSCharsetRule.idl\nWebCore/css/CSSFontFaceLoadEvent.idl\nWebCore/css/CSSFontFaceRule.idl\nWebCore/css/CSSHostRule.idl\nWebCore/css/CSSImportRule.idl\nWebCore/css/CSSMediaRule.idl\nWebCore/css/CSSPageRule.idl\nWebCore/css/CSSPrimitiveValue.idl\nWebCore/css/CSSRule.idl\nWebCore/css/CSSRuleList.idl\nWebCore/css/CSSStyleDeclaration.idl\nWebCore/css/CSSStyleRule.idl\nWebCore/css/CSSStyleSheet.idl\nWebCore/css/CSSSupportsRule.idl\nWebCore/css/CSSUnknownRule.idl\nWebCore/css/CSSValue.idl\nWebCore/css/CSSValueList.idl\nWebCore/css/Counter.idl\nWebCore/css/DOMWindowCSS.idl\nWebCore/css/FontLoader.idl\nWebCore/css/MediaList.idl\nWebCore/css/MediaQueryList.idl\nWebCore/css/MediaQueryListListener.idl\nWebCore/css/RGBColor.idl\nWebCore/css/Rect.idl\nWebCore/css/StyleMedia.idl\nWebCore/css/StyleSheet.idl\nWebCore/css/StyleSheetList.idl\nWebCore/css/WebKitCSSFilterValue.idl\nWebCore/css/WebKitCSSFilterRule.idl\nWebCore/css/WebKitCSSKeyframeRule.idl\nWebCore/css/WebKitCSSKeyframesRule.idl\nWebCore/css/WebKitCSSMatrix.idl\nWebCore/css/WebKitCSSMixFunctionValue.idl\nWebCore/css/WebKitCSSRegionRule.idl\nWebCore/css/WebKitCSSTransformValue.idl\nWebCore/css/WebKitCSSViewportRule.idl\nWebCore/dom/Attr.idl\nWebCore/dom/BeforeLoadEvent.idl\nWebCore/dom/CDATASection.idl\nWebCore/dom/CharacterData.idl\nWebCore/dom/ClientRect.idl\nWebCore/dom/ClientRectList.idl\nWebCore/dom/Clipboard.idl\nWebCore/dom/Comment.idl\nWebCore/dom/CompositionEvent.idl\nWebCore/dom/CustomElementConstructor.idl\nWebCore/dom/CustomEvent.idl\nWebCore/dom/DOMCoreException.idl\nWebCore/dom/DOMError.idl\nWebCore/dom/DOMImplementation.idl\nWebCore/dom/DOMStringList.idl\nWebCore/dom/DOMStringMap.idl\nWebCore/dom/DataTransferItem.idl\nWebCore/dom/DataTransferItemList.idl\nWebCore/dom/DeviceMotionEvent.idl\nWebCore/dom/DeviceOrientationEvent.idl\nWebCore/dom/Document.idl\nWebCore/dom/DocumentFragment.idl\nWebCore/dom/DocumentType.idl\nWebCore/dom/Element.idl\nWebCore/dom/Entity.idl\nWebCore/dom/EntityReference.idl\nWebCore/dom/ErrorEvent.idl\nWebCore/dom/Event.idl\nWebCore/dom/EventException.idl\nWebCore/dom/EventListener.idl\nWebCore/dom/EventTarget.idl\nWebCore/dom/FocusEvent.idl\nWebCore/dom/HashChangeEvent.idl\nWebCore/dom/KeyboardEvent.idl\nWebCore/dom/MessageChannel.idl\nWebCore/dom/MessageEvent.idl\nWebCore/dom/MessagePort.idl\nWebCore/dom/MouseEvent.idl\nWebCore/dom/MutationEvent.idl\nWebCore/dom/MutationObserver.idl\nWebCore/dom/MutationRecord.idl\nWebCore/dom/DOMNamedFlowCollection.idl\nWebCore/dom/NamedNodeMap.idl\nWebCore/dom/Node.idl\nWebCore/dom/NodeFilter.idl\nWebCore/dom/NodeIterator.idl\nWebCore/dom/NodeList.idl\nWebCore/dom/Notation.idl\nWebCore/dom/OverflowEvent.idl\nWebCore/dom/PageTransitionEvent.idl\nWebCore/dom/PopStateEvent.idl\nWebCore/dom/ProcessingInstruction.idl\nWebCore/dom/ProgressEvent.idl\nWebCore/dom/ProgressEvent.idl\nWebCore/dom/PropertyNodeList.idl\nWebCore/dom/Range.idl\nWebCore/dom/RangeException.idl\nWebCore/dom/RequestAnimationFrameCallback.idl\nWebCore/dom/ShadowRoot.idl\nWebCore/dom/StringCallback.idl\nWebCore/dom/Text.idl\nWebCore/dom/TextEvent.idl\nWebCore/dom/Touch.idl\nWebCore/dom/TouchEvent.idl\nWebCore/dom/TouchList.idl\nWebCore/dom/TransitionEvent.idl\nWebCore/dom/TreeWalker.idl\nWebCore/dom/UIEvent.idl\nWebCore/dom/WebKitAnimationEvent.idl\nWebCore/dom/WebKitNamedFlow.idl\nWebCore/dom/WebKitTransitionEvent.idl\nWebCore/dom/WheelEvent.idl\nWebCore/fileapi/Blob.idl\nWebCore/fileapi/File.idl\nWebCore/fileapi/FileError.idl\nWebCore/fileapi/FileException.idl\nWebCore/fileapi/FileList.idl\nWebCore/fileapi/FileReader.idl\nWebCore/fileapi/FileReaderSync.idl\nWebCore/html/DOMFormData.idl\nWebCore/html/DOMSettableTokenList.idl\nWebCore/html/DOMTokenList.idl\nWebCore/html/DOMURL.idl\nWebCore/html/HTMLAllCollection.idl\nWebCore/html/HTMLAnchorElement.idl\nWebCore/html/HTMLAppletElement.idl\nWebCore/html/HTMLAreaElement.idl\nWebCore/html/HTMLAudioElement.idl\nWebCore/html/HTMLBRElement.idl\nWebCore/html/HTMLBaseElement.idl\nWebCore/html/HTMLBaseFontElement.idl\nWebCore/html/HTMLBodyElement.idl\nWebCore/html/HTMLButtonElement.idl\nWebCore/html/HTMLCanvasElement.idl\nWebCore/html/HTMLCollection.idl\nWebCore/html/HTMLDListElement.idl\nWebCore/html/HTMLDataListElement.idl\nWebCore/html/HTMLDetailsElement.idl\nWebCore/html/HTMLDialogElement.idl\nWebCore/html/HTMLDirectoryElement.idl\nWebCore/html/HTMLDivElement.idl\nWebCore/html/HTMLDocument.idl\nWebCore/html/HTMLElement.idl\nWebCore/html/HTMLEmbedElement.idl\nWebCore/html/HTMLFieldSetElement.idl\nWebCore/html/HTMLFontElement.idl\nWebCore/html/HTMLFormControlsCollection.idl\nWebCore/html/HTMLFormElement.idl\nWebCore/html/HTMLFrameElement.idl\nWebCore/html/HTMLFrameSetElement.idl\nWebCore/html/HTMLHRElement.idl\nWebCore/html/HTMLHeadElement.idl\nWebCore/html/HTMLHeadingElement.idl\nWebCore/html/HTMLHtmlElement.idl\nWebCore/html/HTMLIFrameElement.idl\nWebCore/html/HTMLImageElement.idl\nWebCore/html/HTMLInputElement.idl\nWebCore/html/HTMLKeygenElement.idl\nWebCore/html/HTMLLIElement.idl\nWebCore/html/HTMLLabelElement.idl\nWebCore/html/HTMLLegendElement.idl\nWebCore/html/HTMLLinkElement.idl\nWebCore/html/HTMLMapElement.idl\nWebCore/html/HTMLMarqueeElement.idl\nWebCore/html/HTMLMediaElement.idl\nWebCore/html/HTMLMenuElement.idl\nWebCore/html/HTMLMetaElement.idl\nWebCore/html/HTMLMeterElement.idl\nWebCore/html/HTMLModElement.idl\nWebCore/html/HTMLOListElement.idl\nWebCore/html/HTMLObjectElement.idl\nWebCore/html/HTMLOptGroupElement.idl\nWebCore/html/HTMLOptionElement.idl\nWebCore/html/HTMLOptionsCollection.idl\nWebCore/html/HTMLOutputElement.idl\nWebCore/html/HTMLParagraphElement.idl\nWebCore/html/HTMLParamElement.idl\nWebCore/html/HTMLPreElement.idl\nWebCore/html/HTMLProgressElement.idl\nWebCore/html/HTMLPropertiesCollection.idl\nWebCore/html/HTMLQuoteElement.idl\nWebCore/html/HTMLScriptElement.idl\nWebCore/html/HTMLSelectElement.idl\nWebCore/html/HTMLSourceElement.idl\nWebCore/html/HTMLSpanElement.idl\nWebCore/html/HTMLStyleElement.idl\nWebCore/html/HTMLTableCaptionElement.idl\nWebCore/html/HTMLTableCellElement.idl\nWebCore/html/HTMLTableColElement.idl\nWebCore/html/HTMLTableElement.idl\nWebCore/html/HTMLTableRowElement.idl\nWebCore/html/HTMLTableSectionElement.idl\nWebCore/html/HTMLTemplateElement.idl\nWebCore/html/HTMLTextAreaElement.idl\nWebCore/html/HTMLTitleElement.idl\nWebCore/html/HTMLTrackElement.idl\nWebCore/html/HTMLUListElement.idl\nWebCore/html/HTMLUnknownElement.idl\nWebCore/html/HTMLVideoElement.idl\nWebCore/html/ImageData.idl\nWebCore/html/MediaController.idl\nWebCore/html/MediaError.idl\nWebCore/html/MediaKeyError.idl\nWebCore/html/MediaKeyEvent.idl\nWebCore/html/MicroDataItemValue.idl\nWebCore/html/RadioNodeList.idl\nWebCore/html/TextMetrics.idl\nWebCore/html/TimeRanges.idl\nWebCore/html/ValidityState.idl\nWebCore/html/VoidCallback.idl\nWebCore/html/canvas/ArrayBuffer.idl\nWebCore/html/canvas/ArrayBufferView.idl\nWebCore/html/canvas/CanvasGradient.idl\nWebCore/html/canvas/CanvasPattern.idl\nWebCore/html/canvas/CanvasProxy.idl\nWebCore/html/canvas/CanvasRenderingContext.idl\nWebCore/html/canvas/CanvasRenderingContext2D.idl\nWebCore/html/canvas/DataView.idl\nWebCore/html/canvas/DOMPath.idl\nWebCore/html/canvas/EXTDrawBuffers.idl\nWebCore/html/canvas/EXTTextureFilterAnisotropic.idl\nWebCore/html/canvas/Float32Array.idl\nWebCore/html/canvas/Float64Array.idl\nWebCore/html/canvas/Int16Array.idl\nWebCore/html/canvas/Int32Array.idl\nWebCore/html/canvas/Int8Array.idl\nWebCore/html/canvas/OESElementIndexUint.idl\nWebCore/html/canvas/OESStandardDerivatives.idl\nWebCore/html/canvas/OESTextureFloat.idl\nWebCore/html/canvas/OESTextureHalfFloat.idl\nWebCore/html/canvas/OESVertexArrayObject.idl\nWebCore/html/canvas/Uint16Array.idl\nWebCore/html/canvas/Uint32Array.idl\nWebCore/html/canvas/Uint8Array.idl\nWebCore/html/canvas/Uint8ClampedArray.idl\nWebCore/html/canvas/WebGLActiveInfo.idl\nWebCore/html/canvas/WebGLBuffer.idl\nWebCore/html/canvas/WebGLCompressedTextureATC.idl\nWebCore/html/canvas/WebGLCompressedTexturePVRTC.idl\nWebCore/html/canvas/WebGLCompressedTextureS3TC.idl\nWebCore/html/canvas/WebGLContextAttributes.idl\nWebCore/html/canvas/WebGLContextEvent.idl\nWebCore/html/canvas/WebGLDepthTexture.idl\nWebCore/html/canvas/WebGLFramebuffer.idl\nWebCore/html/canvas/WebGLLoseContext.idl\nWebCore/html/canvas/WebGLProgram.idl\nWebCore/html/canvas/WebGLRenderbuffer.idl\nWebCore/html/canvas/WebGLRenderingContext.idl\nWebCore/html/canvas/WebGLShader.idl\nWebCore/html/canvas/WebGLShaderPrecisionFormat.idl\nWebCore/html/canvas/WebGLTexture.idl\nWebCore/html/canvas/WebGLUniformLocation.idl\nWebCore/html/canvas/WebGLVertexArrayObjectOES.idl\nWebCore/html/shadow/HTMLContentElement.idl\nWebCore/html/shadow/HTMLShadowElement.idl\nWebCore/html/track/TextTrack.idl\nWebCore/html/track/TextTrackCue.idl\nWebCore/html/track/TextTrackCueList.idl\nWebCore/html/track/TextTrackList.idl\nWebCore/html/track/TrackEvent.idl\nWebCore/inspector/InjectedScriptHost.idl\nWebCore/inspector/InspectorFrontendHost.idl\nWebCore/inspector/ScriptProfile.idl\nWebCore/inspector/ScriptProfileNode.idl\nWebCore/loader/appcache/DOMApplicationCache.idl\nWebCore/page/AbstractView.idl\nWebCore/page/BarInfo.idl\nWebCore/page/Console.idl\nWebCore/page/Crypto.idl\nWebCore/page/DOMSecurityPolicy.idl\nWebCore/page/DOMSelection.idl\nWebCore/page/DOMWindow.idl\nWebCore/page/EventSource.idl\nWebCore/page/History.idl\nWebCore/page/Location.idl\nWebCore/page/Navigator.idl\nWebCore/page/Performance.idl\nWebCore/page/PerformanceNavigation.idl\nWebCore/page/PerformanceTiming.idl\nWebCore/page/Screen.idl\nWebCore/page/SpeechInputEvent.idl\nWebCore/page/SpeechInputResult.idl\nWebCore/page/SpeechInputResultList.idl\nWebCore/page/WebKitPoint.idl\nWebCore/page/WorkerNavigator.idl\nWebCore/plugins/DOMMimeType.idl\nWebCore/plugins/DOMMimeTypeArray.idl\nWebCore/plugins/DOMPlugin.idl\nWebCore/plugins/DOMPluginArray.idl\nWebCore/storage/Storage.idl\nWebCore/storage/StorageEvent.idl\nWebCore/svg/ElementTimeControl.idl\nWebCore/svg/SVGAElement.idl\nWebCore/svg/SVGAltGlyphDefElement.idl\nWebCore/svg/SVGAltGlyphElement.idl\nWebCore/svg/SVGAltGlyphItemElement.idl\nWebCore/svg/SVGAngle.idl\nWebCore/svg/SVGAnimateColorElement.idl\nWebCore/svg/SVGAnimateElement.idl\nWebCore/svg/SVGAnimateMotionElement.idl\nWebCore/svg/SVGAnimateTransformElement.idl\nWebCore/svg/SVGAnimatedAngle.idl\nWebCore/svg/SVGAnimatedBoolean.idl\nWebCore/svg/SVGAnimatedEnumeration.idl\nWebCore/svg/SVGAnimatedInteger.idl\nWebCore/svg/SVGAnimatedLength.idl\nWebCore/svg/SVGAnimatedLengthList.idl\nWebCore/svg/SVGAnimatedNumber.idl\nWebCore/svg/SVGAnimatedNumberList.idl\nWebCore/svg/SVGAnimatedPreserveAspectRatio.idl\nWebCore/svg/SVGAnimatedRect.idl\nWebCore/svg/SVGAnimatedString.idl\nWebCore/svg/SVGAnimatedTransformList.idl\nWebCore/svg/SVGAnimationElement.idl\nWebCore/svg/SVGCircleElement.idl\nWebCore/svg/SVGClipPathElement.idl\nWebCore/svg/SVGColor.idl\nWebCore/svg/SVGComponentTransferFunctionElement.idl\nWebCore/svg/SVGCursorElement.idl\nWebCore/svg/SVGDefsElement.idl\nWebCore/svg/SVGDescElement.idl\nWebCore/svg/SVGDocument.idl\nWebCore/svg/SVGElement.idl\nWebCore/svg/SVGElementInstance.idl\nWebCore/svg/SVGEllipseElement.idl\nWebCore/svg/SVGException.idl\nWebCore/svg/SVGExternalResourcesRequired.idl\nWebCore/svg/SVGFEBlendElement.idl\nWebCore/svg/SVGFEColorMatrixElement.idl\nWebCore/svg/SVGFEComponentTransferElement.idl\nWebCore/svg/SVGFECompositeElement.idl\nWebCore/svg/SVGFEConvolveMatrixElement.idl\nWebCore/svg/SVGFEDiffuseLightingElement.idl\nWebCore/svg/SVGFEDisplacementMapElement.idl\nWebCore/svg/SVGFEDistantLightElement.idl\nWebCore/svg/SVGFEDropShadowElement.idl\nWebCore/svg/SVGFEFloodElement.idl\nWebCore/svg/SVGFEFuncAElement.idl\nWebCore/svg/SVGFEFuncBElement.idl\nWebCore/svg/SVGFEFuncGElement.idl\nWebCore/svg/SVGFEFuncRElement.idl\nWebCore/svg/SVGFEGaussianBlurElement.idl\nWebCore/svg/SVGFEImageElement.idl\nWebCore/svg/SVGFEMergeElement.idl\nWebCore/svg/SVGFEMergeNodeElement.idl\nWebCore/svg/SVGFEMorphologyElement.idl\nWebCore/svg/SVGFEOffsetElement.idl\nWebCore/svg/SVGFEPointLightElement.idl\nWebCore/svg/SVGFESpecularLightingElement.idl\nWebCore/svg/SVGFESpotLightElement.idl\nWebCore/svg/SVGFETileElement.idl\nWebCore/svg/SVGFETurbulenceElement.idl\nWebCore/svg/SVGFilterElement.idl\nWebCore/svg/SVGFilterPrimitiveStandardAttributes.idl\nWebCore/svg/SVGFitToViewBox.idl\nWebCore/svg/SVGFontElement.idl\nWebCore/svg/SVGFontFaceElement.idl\nWebCore/svg/SVGFontFaceFormatElement.idl\nWebCore/svg/SVGFontFaceNameElement.idl\nWebCore/svg/SVGFontFaceSrcElement.idl\nWebCore/svg/SVGFontFaceUriElement.idl\nWebCore/svg/SVGForeignObjectElement.idl\nWebCore/svg/SVGGElement.idl\nWebCore/svg/SVGGlyphElement.idl\nWebCore/svg/SVGGlyphRefElement.idl\nWebCore/svg/SVGGradientElement.idl\nWebCore/svg/SVGHKernElement.idl\nWebCore/svg/SVGImageElement.idl\nWebCore/svg/SVGLangSpace.idl\nWebCore/svg/SVGLength.idl\nWebCore/svg/SVGLengthList.idl\nWebCore/svg/SVGLineElement.idl\nWebCore/svg/SVGLinearGradientElement.idl\nWebCore/svg/SVGLocatable.idl\nWebCore/svg/SVGMPathElement.idl\nWebCore/svg/SVGMarkerElement.idl\nWebCore/svg/SVGMaskElement.idl\nWebCore/svg/SVGMatrix.idl\nWebCore/svg/SVGMetadataElement.idl\nWebCore/svg/SVGMissingGlyphElement.idl\nWebCore/svg/SVGNumber.idl\nWebCore/svg/SVGNumberList.idl\nWebCore/svg/SVGPaint.idl\nWebCore/svg/SVGPathElement.idl\nWebCore/svg/SVGPathSeg.idl\nWebCore/svg/SVGPathSegArcAbs.idl\nWebCore/svg/SVGPathSegArcRel.idl\nWebCore/svg/SVGPathSegClosePath.idl\nWebCore/svg/SVGPathSegCurvetoCubicAbs.idl\nWebCore/svg/SVGPathSegCurvetoCubicRel.idl\nWebCore/svg/SVGPathSegCurvetoCubicSmoothAbs.idl\nWebCore/svg/SVGPathSegCurvetoCubicSmoothRel.idl\nWebCore/svg/SVGPathSegCurvetoQuadraticAbs.idl\nWebCore/svg/SVGPathSegCurvetoQuadraticRel.idl\nWebCore/svg/SVGPathSegCurvetoQuadraticSmoothAbs.idl\nWebCore/svg/SVGPathSegCurvetoQuadraticSmoothRel.idl\nWebCore/svg/SVGPathSegLinetoAbs.idl\nWebCore/svg/SVGPathSegLinetoHorizontalAbs.idl\nWebCore/svg/SVGPathSegLinetoHorizontalRel.idl\nWebCore/svg/SVGPathSegLinetoRel.idl\nWebCore/svg/SVGPathSegLinetoVerticalAbs.idl\nWebCore/svg/SVGPathSegLinetoVerticalRel.idl\nWebCore/svg/SVGPathSegList.idl\nWebCore/svg/SVGPathSegMovetoAbs.idl\nWebCore/svg/SVGPathSegMovetoRel.idl\nWebCore/svg/SVGPatternElement.idl\nWebCore/svg/SVGPoint.idl\nWebCore/svg/SVGPointList.idl\nWebCore/svg/SVGPolygonElement.idl\nWebCore/svg/SVGPolylineElement.idl\nWebCore/svg/SVGPreserveAspectRatio.idl\nWebCore/svg/SVGRadialGradientElement.idl\nWebCore/svg/SVGRect.idl\nWebCore/svg/SVGRectElement.idl\nWebCore/svg/SVGRenderingIntent.idl\nWebCore/svg/SVGSVGElement.idl\nWebCore/svg/SVGScriptElement.idl\nWebCore/svg/SVGSetElement.idl\nWebCore/svg/SVGStopElement.idl\nWebCore/svg/SVGStringList.idl\nWebCore/svg/SVGStyleElement.idl\nWebCore/svg/SVGStyledElement.idl\nWebCore/svg/SVGSwitchElement.idl\nWebCore/svg/SVGSymbolElement.idl\nWebCore/svg/SVGTRefElement.idl\nWebCore/svg/SVGTSpanElement.idl\nWebCore/svg/SVGTests.idl\nWebCore/svg/SVGTextContentElement.idl\nWebCore/svg/SVGTextElement.idl\nWebCore/svg/SVGTextPathElement.idl\nWebCore/svg/SVGTextPositioningElement.idl\nWebCore/svg/SVGTitleElement.idl\nWebCore/svg/SVGTransform.idl\nWebCore/svg/SVGTransformList.idl\nWebCore/svg/SVGTransformable.idl\nWebCore/svg/SVGURIReference.idl\nWebCore/svg/SVGUnitTypes.idl\nWebCore/svg/SVGUseElement.idl\nWebCore/svg/SVGVKernElement.idl\nWebCore/svg/SVGViewElement.idl\nWebCore/svg/SVGViewSpec.idl\nWebCore/svg/SVGZoomAndPan.idl\nWebCore/svg/SVGZoomEvent.idl\nWebCore/testing/Internals.idl\nWebCore/testing/InternalSettings.idl\nWebCore/testing/MallocStatistics.idl\nWebCore/testing/MemoryInfo.idl\nWebCore/testing/TypeConversions.idl\nWebCore/workers/AbstractWorker.idl\nWebCore/workers/DedicatedWorkerContext.idl\nWebCore/workers/SharedWorker.idl\nWebCore/workers/SharedWorkerContext.idl\nWebCore/workers/Worker.idl\nWebCore/workers/WorkerContext.idl\nWebCore/workers/WorkerLocation.idl\nWebCore/xml/DOMParser.idl\nWebCore/xml/XMLHttpRequest.idl\nWebCore/xml/XMLHttpRequestException.idl\nWebCore/xml/XMLHttpRequestProgressEvent.idl\nWebCore/xml/XMLHttpRequestUpload.idl\nWebCore/xml/XMLSerializer.idl\nWebCore/xml/XPathEvaluator.idl\nWebCore/xml/XPathException.idl\nWebCore/xml/XPathExpression.idl\nWebCore/xml/XPathNSResolver.idl\nWebCore/xml/XPathResult.idl\nWebCore/xml/XSLTProcessor.idl\nInternalSettingsGenerated.idl\nWebCore/inspector/JavaScriptCallFrame.idl\n" > ./idl_files.tmp
perl JavaScriptCore/docs/make-bytecode-docs.pl JavaScriptCore/interpreter/Interpreter.cpp docs/bytecode.html
cat WebCore/css/CSSPropertyNames.in WebCore/css/SVGCSSPropertyNames.in > CSSPropertyNames.in
rm -f ./idl_files.tmp
python JavaScriptCore/KeywordLookupGenerator.py JavaScriptCore/parser/Keywords.table > KeywordLookup.h
sed -e s/\<WebCore/\<WebKit/ -e s/DOMDOMImplementation/DOMImplementation/ /Volumes/Data/Build/Release/WebCore.framework/PrivateHeaders/DOM.h > /Volumes/Data/Build/Release/WebKit.framework/Versions/A/Headers/DOM.h
END

for my $line (@expectIgnoredLines) {
    is(shouldIgnoreLine("", $line), 1, description("Ignored: " . $line));
}

#
# Test input starting with four spaces
#
my @buildSettingsLines = split(/$INPUT_RECORD_SEPARATOR/, <<'END');
Build settings from command line:
    ARCHS = i386 x86_64
    OBJROOT = /Volumes/Data/Build
    ONLY_ACTIVE_ARCH = NO
    SHARED_PRECOMPS_DIR = /Volumes/Data/Build/PrecompiledHeaders
    SYMROOT = /Volumes/Data/Build
END

for my $i (0..scalar(@buildSettingsLines) - 1) {
    my $previousLine = $i ? $buildSettingsLines[$i - 1] : "";
    my $line = $buildSettingsLines[$i];
    is(shouldIgnoreLine($previousLine, $line), 1, description("Ignored: " . $line));
}

#
# Test input for undefined symbols error message
#
my @undefinedSymbolsLines = split(/$INPUT_RECORD_SEPARATOR/, <<'END');
Undefined symbols for architecture x86_64:
  "__ZN6WebKit12WebPageProxy28exposedRectChangedTimerFiredEPN7WebCore5TimerIS0_EE", referenced from:
      __ZN6WebKit12WebPageProxyC2EPNS_10PageClientEN3WTF10PassRefPtrINS_15WebProcessProxyEEEPNS_12WebPageGroupEy in WebPageProxy.o
ld: symbol(s) not found for architecture x86_64
clang: error: linker command failed with exit code 1 (use -v to see invocation)
END

for my $i (0..scalar(@undefinedSymbolsLines) - 1) {
    my $previousLine = $i ? $undefinedSymbolsLines[$i - 1] : "";
    my $line = $undefinedSymbolsLines[$i];
    is(shouldIgnoreLine($previousLine, $line), 0, description("Printed: " . $line));
}

my @ruleScriptLines = split(/$INPUT_RECORD_SEPARATOR/, <<'END');
RuleScriptExecution /Users/u/Build/Debug/LLIntOffsets/arm64e/LLIntDesiredOffsets.h /Users/u/Build/JavaScriptCore.build/Debug/JSCLLIntOffsetsExtractor.build/LowLevelInterpreter.d /Users/u/WebKit/OpenSource/Source/JavaScriptCore/llint/LowLevelInterpreter.asm normal undefined_arch (in target 'JSCLLIntOffsetsExtractor' from project 'JavaScriptCore')
    cd /Users/u/WebKit/OpenSource/Source/JavaScriptCore
    /bin/sh -c set\ -e'
''
'OFFLINEASM_ARGS\=\"\"'
'if\ \[\[\ \"\$\{DEPLOYMENT_LOCATION\}\"\ \=\=\ \"YES\"\ \]\]\;\ then'
'\ \ \ \ OFFLINEASM_ARGS\=\"\$\{OFFLINEASM_ARGS\}\ --webkit-additions-path\=\$\{WK_WEBKITADDITIONS_HEADERS_FOLDER_PATH\}\"'
'fi'
''
'/usr/bin/env\ ruby\ \"\$\{SRCROOT\}/offlineasm/generate_offset_extractor.rb\"\ \"-I\$\{BUILT_PRODUCTS_DIR\}/DerivedSources/JavaScriptCore\"\ \"\$\{INPUT_FILE_PATH\}\"\ \ \"\$\{BUILT_PRODUCTS_DIR\}/JSCLLIntSettingsExtractor\"\ \"\$\{SCRIPT_OUTPUT_FILE_0\}\"\ \"\$\{ARCHS\}\ C_LOOP\"\ \"\$\{BUILD_VARIANTS\}\"\ \$\{OFFLINEASM_ARGS\}\ --depfile\=\"\$\{TARGET_TEMP_DIR\}/\$\{INPUT_FILE_BASE\}.d\"'
''
'
RuleScriptExecution /Users/u/Build/Debug/usr/local/include/absl/utility/utility.h /Users/u/WebKit/OpenSource/Source/ThirdParty/libwebrtc/Source/third_party/abseil-cpp/absl/utility/utility.h normal undefined_arch (in target 'absl' from project 'libwebrtc')
    cd /Users/u/WebKit/OpenSource/Source/ThirdParty/libwebrtc
    /bin/sh -c cp\ -f\ \"\$\{INPUT_FILE_PATH\}\"\ \"\$\{SCRIPT_OUTPUT_FILE_0\}\"'
'
RuleScriptExecution stoptest.h
END

for my $i (0..scalar(@ruleScriptLines) - 1) {
    my $previousLine = $i ? $ruleScriptLines[$i - 1] : "";
    my $line = $ruleScriptLines[$i];
    if ($line =~ /RuleScriptExecution/) {
        is(shouldIgnoreLine($previousLine, $line), 0, description("Printed: " . $line));
    } else {
        is(shouldIgnoreLine($previousLine, $line), 1, description("Ignored: " . $line));
    }
}

# Investigate these in https://bugs.webkit.org/show_bug.cgi?id=245263.
my @diagProblemLines = split(/$INPUT_RECORD_SEPARATOR/, <<'END');
CompileC /Users/u/Build/WebCore.build/Debug/WebCore.build/Objects-normal/arm64e/UnifiedSource245.o /Users/u/Build/Debug/DerivedSources/WebCore/unified-sources/UnifiedSource245.cpp normal arm64e c++ com.apple.compilers.llvm.clang.1_0.compiler (in target 'WebCore' from project 'WebCore')
    cd /Users/u/WebKit/OpenSource/Source/WebCore
    /Volumes/Xcode14A6270e_m20A2411_m22A338_i20A358_FastSim_Boost_Encrypted_53GB/Xcode.app/Contents/Developer/Toolchains/OSX13.0.xctoolchain/usr/bin/clang -x c++ -target arm64e-apple-macos13.0 -fmessage-length\=0 -fdiagnostics-show-note-include-stack -fmacro-backtrace-limit\=0 -std\=c++2b -stdlib\=libc++ -gmodules -Wno-trigraphs -fno-exceptions -fno-rtti -fno-sanitize\=vptr -fpascal-strings -O0 -fno-common -Werror -Wno-missing-field-initializers -Wmissing-prototypes -Wunreachable-code -Wnon-virtual-dtor -Wno-overloaded-virtual -Wno-exit-time-destructors -Wno-missing-braces -Wparentheses -Wswitch -Wunused-function -Wno-unused-label -Wno-unused-parameter -Wunused-variable -Wunused-value -Wempty-body -Wuninitialized -Wno-unknown-pragmas -Wno-shadow -Wno-four-char-constants -Wno-conversion -Wconstant-conversion -Wint-conversion -Wbool-conversion -Wenum-conversion -Wno-float-conversion -Wnon-literal-null-conversion -Wobjc-literal-conversion -Wsign-compare -Wno-shorten-64-to-32 -Wnewline-eof -Wno-c++11-extensions -Wno-implicit-fallthrough -DBUILDING_WEBKIT -DGL_SILENCE_DEPRECATION\=1 -DGLES_SILENCE_DEPRECATION\=1 -isysroot /Volumes/Xcode14A6270e_m20A2411_m22A338_i20A358_FastSim_Boost_Encrypted_53GB/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX13.0.Internal.sdk -fstrict-aliasing -Wdeprecated-declarations -Winvalid-offsetof -g -fvisibility\=hidden -fvisibility-inlines-hidden -fno-threadsafe-statics -Wno-sign-conversion -Winfinite-recursion -Wmove -Wcomma -Wblock-capture-autoreleasing -Wstrict-prototypes -Wrange-loop-analysis -Wno-semicolon-before-method-body -index-store-path /Users/u/Library/Developer/Xcode/DerivedData/WebKit-hbntwurqoeetjbbukcpuwpfssnio/Index.noindex/DataStore -iquote /Users/u/Build/WebCore.build/Debug/WebCore.build/WebCore-generated-files.hmap -I/Users/u/Build/WebCore.build/Debug/WebCore.build/WebCore-own-target-headers.hmap -I/Users/u/Build/WebCore.build/Debug/WebCore.build/WebCore-all-target-headers.hmap -iquote /Users/u/Build/WebCore.build/Debug/WebCore.build/WebCore-project-headers.hmap -I/Users/u/Build/Debug/include -IPAL -IForwardingHeaders -I/Volumes/Xcode14A6270e_m20A2411_m22A338_i20A358_FastSim_Boost_Encrypted_53GB/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX13.0.Internal.sdk/usr/include/libxslt -I/Volumes/Xcode14A6270e_m20A2411_m22A338_i20A358_FastSim_Boost_Encrypted_53GB/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX13.0.Internal.sdk/usr/include/libxml2 -I/Users/u/Build/Debug/DerivedSources/WebCore -I/Users/u/Build/Debug/usr/local/include -I/Users/u/Build/Debug/usr/local/include/WebKitAdditions -I/Volumes/Xcode14A6270e_m20A2411_m22A338_i20A358_FastSim_Boost_Encrypted_53GB/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX13.0.Internal.sdk/usr/local/include/WebKitAdditions -I/Users/u/Build/Debug/usr/local/include -I/Volumes/Xcode14A6270e_m20A2411_m22A338_i20A358_FastSim_Boost_Encrypted_53GB/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX13.0.Internal.sdk/usr/local/include -I/Users/u/Build/Debug/usr/local/include/webrtc -I/Volumes/Xcode14A6270e_m20A2411_m22A338_i20A358_FastSim_Boost_Encrypted_53GB/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX13.0.Internal.sdk/usr/local/include/webrtc -I/Users/u/Build/Debug/usr/local/include/webrtc/sdk/objc/Framework/Headers -I/Volumes/Xcode14A6270e_m20A2411_m22A338_i20A358_FastSim_Boost_Encrypted_53GB/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX13.0.Internal.sdk/usr/local/include/webrtc/sdk/objc/Framework/Headers -I/Users/u/WebKit/OpenSource/Source/WebCore -I/Users/u/Build/WebCore.build/Debug/WebCore.build/DerivedSources-normal/arm64e -I/Users/u/Build/WebCore.build/Debug/WebCore.build/DerivedSources/arm64e -I/Users/u/Build/WebCore.build/Debug/WebCore.build/DerivedSources -Wall -Wextra -Wcast-qual -Wchar-subscripts -Wconditional-uninitialized -Wextra-tokens -Wformat\=2 -Winit-self -Wmissing-format-attribute -Wmissing-noreturn -Wpacked -Wpointer-arith -Wredundant-decls -Wundef -Wwrite-strings -Wexit-time-destructors -Wglobal-constructors -Wtautological-compare -Wimplicit-fallthrough -Wvla -Wno-unknown-warning-option -Wliteral-conversion -Wthread-safety -Wno-profile-instr-out-of-date -Wno-profile-instr-unprofiled -F/Users/u/Build/Debug -iframework /Volumes/Xcode14A6270e_m20A2411_m22A338_i20A358_FastSim_Boost_Encrypted_53GB/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX13.0.Internal.sdk/System/Library/PrivateFrameworks -iframework /Volumes/Xcode14A6270e_m20A2411_m22A338_i20A358_FastSim_Boost_Encrypted_53GB/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX13.0.Internal.sdk/Library/Apple/System/Library/PrivateFrameworks -iframework /Volumes/Xcode14A6270e_m20A2411_m22A338_i20A358_FastSim_Boost_Encrypted_53GB/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX13.0.Internal.sdk/System/Library/Frameworks -isystem /Volumes/Xcode14A6270e_m20A2411_m22A338_i20A358_FastSim_Boost_Encrypted_53GB/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX13.0.Internal.sdk/System/Library/Frameworks/System.framework/PrivateHeaders -include /Users/u/Build/PrecompiledHeaders/SharedPrecompiledHeaders/15890480973332163591/WebCorePrefix.h -MMD -MT dependencies -MF /Users/u/Build/WebCore.build/Debug/WebCore.build/Objects-normal/arm64e/UnifiedSource245.d --serialize-diagnostics /Users/u/Build/WebCore.build/Debug/WebCore.build/Objects-normal/arm64e/UnifiedSource245.dia -c /Users/u/Build/Debug/DerivedSources/WebCore/unified-sources/UnifiedSource245.cpp -o /Users/u/Build/WebCore.build/Debug/WebCore.build/Objects-normal/arm64e/UnifiedSource245.o -index-unit-output-path /WebCore.build/Debug/WebCore.build/Objects-normal/arm64e/UnifiedSource245.o
/Users/u/Build/WebCore.build/Debug/WebCore.build/Objects-normal/arm64e/UnifiedSource245.dia:1:1: warning: Could not read serialized diagnostics file: error("Failed to open diagnostics file") (in target 'WebCore' from project 'WebCore')

CompileC stoptest
END

for my $i (0..scalar(@diagProblemLines) - 1) {
    my $previousLine = $i ? $diagProblemLines[$i - 1] : "";
    my $line = $diagProblemLines[$i];
    if ($line =~ /CompileC/) {
        is(shouldIgnoreLine($previousLine, $line), 0, description("Printed: " . $line));
    } else {
        is(shouldIgnoreLine($previousLine, $line), 1, description("Ignored: " . $line));
    }
}

my @productPackagingLines = split(/$INPUT_RECORD_SEPARATOR/, <<'END');
ProcessProductPackaging "" /Users/u/Build/libwebrtc.build/Debug/yasm.build/yasm.xcent (in target 'yasm' from project 'libwebrtc')
    cd /Users/u/WebKit/OpenSource/Source/ThirdParty/libwebrtc

    Entitlements:

    {
    "com.apple.security.get-task-allow" = 1;
}

    builtin-productPackagingUtility -entitlements -format xml -o /Users/u/Build/libwebrtc.build/Debug/yasm.build/yasm.xcent

ProcessProductPackaging stoptest.h
END
for my $i (0..scalar(@productPackagingLines) - 1) {
    my $previousLine = $i ? $productPackagingLines[$i - 1] : "";
    my $line = $productPackagingLines[$i];
    if ($line =~ /ProcessProductPackaging/) {
        is(shouldIgnoreLine($previousLine, $line), 0, description("Printed: " . $line));
    } else {
        is(shouldIgnoreLine($previousLine, $line), 1, description("Ignored: " . $line));
    }
}


my @processProductPackagingDER = split(/$INPUT_RECORD_SEPARATOR/, <<'END');
ProcessProductPackagingDER /Users/u/Build/DumpRenderTree.build/Debug/DumpRenderTree.app.build/DumpRenderTree.app.xcent /Users/u/Build/DumpRenderTree.build/Debug/DumpRenderTree.app.build/DumpRenderTree.app.xcent.der (in target 'DumpRenderTree.app' from project 'DumpRenderTree')
    cd /Users/u/WebKit/OpenSource/Tools/DumpRenderTree
    /usr/bin/derq query -f xml -i /Users/u/Build/DumpRenderTree.build/Debug/DumpRenderTree.app.build/DumpRenderTree.app.xcent -o /Users/u/Build/DumpRenderTree.build/Debug/DumpRenderTree.app.build/DumpRenderTree.app.xcent.der --raw

ProcessProductPackagingDER stoptest
END
for my $i (0..scalar(@processProductPackagingDER) - 1) {
    my $previousLine = $i ? $processProductPackagingDER[$i - 1] : "";
    my $line = $processProductPackagingDER[$i];
    if ($line =~ /ProcessProductPackagingDER/) {
        is(shouldIgnoreLine($previousLine, $line), 0, description("Printed: " . $line));
    } else {
        is(shouldIgnoreLine($previousLine, $line), 1, description("Ignored: " . $line));
    }
}

# Two below once https://bugs.webkit.org/show_bug.cgi?id=175997 is fixed.
my @libtoolSameMemberLines = split(/$INPUT_RECORD_SEPARATOR/, <<'END');
Libtool /Users/u/Build/Debug/libWTF.a normal (in target 'WTF' from project 'WTF')
    cd /Users/u/WebKit/OpenSource/Source/WTF
    /Volumes/Xcode14A6270e_m20A2411_m22A338_i20A358_FastSim_Boost_Encrypted_53GB/Xcode.app/Contents/Developer/Toolchains/OSX13.0.xctoolchain/usr/bin/libtool -static -arch_only arm64e -D -syslibroot /Volumes/Xcode14A6270e_m20A2411_m22A338_i20A358_FastSim_Boost_Encrypted_53GB/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX13.0.Internal.sdk -L/Users/u/Build/Debug -L/Volumes/Xcode14A6270e_m20A2411_m22A338_i20A358_FastSim_Boost_Encrypted_53GB/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX13.0.Internal.sdk/usr/local/lib -filelist /Users/u/Build/WTF.build/Debug/WTF.build/Objects-normal/arm64e/WTF.LinkFileList -lbmalloc -dependency_info /Users/u/Build/WTF.build/Debug/WTF.build/Objects-normal/arm64e/WTF_libtool_dependency_info.dat -o /Users/u/Build/Debug/libWTF.a
/Volumes/Xcode14A6270e_m20A2411_m22A338_i20A358_FastSim_Boost_Encrypted_53GB/Xcode.app/Contents/Developer/Toolchains/OSX13.0.xctoolchain/usr/bin/libtool: warning same member name (Gigacage.o) in output file used for input files: /Users/u/Build/WTF.build/Debug/WTF.build/Objects-normal/arm64e/Gigacage.o and: /Users/u/Build/Debug/libbmalloc.a(Gigacage.o) due to use of basename, truncation and blank padding
/Volumes/Xcode14A6270e_m20A2411_m22A338_i20A358_FastSim_Boost_Encrypted_53GB/Xcode.app/Contents/Developer/Toolchains/OSX13.0.xctoolchain/usr/bin/libtool: warning same member name (Logging.o) in output file used for input files: /Users/u/Build/WTF.build/Debug/WTF.build/Objects-normal/arm64e/Logging.o and: /Users/u/Build/Debug/libbmalloc.a(Logging.o) due to use of basename, truncation and blank padding

Libtool stoptest
END

for my $i (0..scalar(@libtoolSameMemberLines) - 1) {
    my $previousLine = $i ? $libtoolSameMemberLines[$i - 1] : "";
    my $line = $libtoolSameMemberLines[$i];
    if ($line =~ /Libtool/) {
        is(shouldIgnoreLine($previousLine, $line), 0, description("Printed: " . $line));
    } else {
        is(shouldIgnoreLine($previousLine, $line), 1, description("Ignored: " . $line));
    }
}

my @dsymNoObjectFileLines = split(/$INPUT_RECORD_SEPARATOR/, <<'END');
GenerateDSYMFile /Users/u/Build/Debug/TestWTF.dSYM /Users/u/Build/Debug/TestWTF (in target 'TestWTF' from project 'TestWebKitAPI')
    cd /Users/u/WebKit/OpenSource/Tools/TestWebKitAPI
    /Volumes/Xcode14A6270e_m20A2411_m22A338_i20A358_FastSim_Boost_Encrypted_53GB/Xcode.app/Contents/Developer/Toolchains/OSX13.0.xctoolchain/usr/bin/dsymutil /Users/u/Build/Debug/TestWTF -o /Users/u/Build/Debug/TestWTF.dSYM
warning: (arm64e)  could not find object file symbol for symbol __ZNK3WTF10AtomString23convertToASCIILowercaseEv
warning: (arm64e)  could not find object file symbol for symbol __ZNK3WTF10AtomString16convertASCIICaseILNS0_15CaseConvertTypeE1EEES0_v
warning: (arm64e)  could not find object file symbol for symbol __ZNK3WTF10AtomString23convertToASCIIUppercaseEv
warning: (arm64e)  could not find object file symbol for symbol __ZNK3WTF10AtomString16convertASCIICaseILNS0_15CaseConvertTypeE0EEES0_v
warning: (arm64e)  could not find object file symbol for symbol __ZN3WTF10AtomString6numberEi
warning: (arm64e)  could not find object file symbol for symbol __ZN3WTF20numberToStringSignedINS_10AtomStringEiEENS_30IntegerToStringConversionTraitIT_E10ReturnTypeET0_PNS4_22AdditionalArgumentTypeE
warning: (arm64e)  could not find object file symbol for symbol __ZN3WTF10AtomString6numberEj

GenerateDSYMFile stoptest
END
for my $i (0..scalar(@dsymNoObjectFileLines) - 1) {
    my $previousLine = $i ? $dsymNoObjectFileLines[$i - 1] : "";
    my $line = $dsymNoObjectFileLines[$i];
    if ($line =~ /GenerateDSYMFile/) {
        is(shouldIgnoreLine($previousLine, $line), 0, description("Printed: " . $line));
    } else {
        is(shouldIgnoreLine($previousLine, $line), 1, description("Ignored: " . $line));
    }
}

my @buildDescriptionLines = split(/$INPUT_RECORD_SEPARATOR/, <<'END');
Create build description
Build description signature: 6df54043eda5ce9a5bc62735755fc500
Build description path: /Users/u/Build/XCBuildData/6df54043eda5ce9a5bc62735755fc500-desc.xcbuild
note: Building targets in dependency order
Create build description
END
for my $i (0..scalar(@buildDescriptionLines) - 1) {
    my $previousLine = $i ? $dsymNoObjectFileLines[$i - 1] : "";
    my $line = $buildDescriptionLines[$i];
    if ($line =~ /Create build description/) {
        is(shouldIgnoreLine($previousLine, $line), 0, description("Printed: " . $line));
    } else {
        is(shouldIgnoreLine($previousLine, $line), 1, description("Ignored: " . $line));
    }
}
my @libtoolEmptyTOC = split(/$INPUT_RECORD_SEPARATOR/, <<'END');
Libtool libWebKitAdditions
warning: /Volumes/Somevolume/Xcode.app/Contents/Developer/Toolchains/OSX13.0.xctoolchain/usr/bin/libtool: archive library: /Users/u/Build/Debug/libWebKitAdditions.a the table of contents is empty (no object file members in the library define global symbols)
Libtool endtest
END
for my $i (0..scalar(@libtoolEmptyTOC) - 1) {
    my $previousLine = $i ? $libtoolEmptyTOC[$i - 1] : "";
    my $line = $libtoolEmptyTOC[$i];
    if ($line =~ /Libtool/) {
        is(shouldIgnoreLine($previousLine, $line), 0, description("Printed: " . $line));
    } else {
        is(shouldIgnoreLine($previousLine, $line), 1, description("Ignored: " . $line));
    }
}

my @generatePrefsLines = split(/$INPUT_RECORD_SEPARATOR/, <<'END');
PhaseScriptExecution Run\ Script /Users/u/Build/DumpRenderTree.build/Debug/Derived\ Sources.build/Script-0F18E7011D6B9CC60027E547.sh (in target 'Derived Sources' from project 'DumpRenderTree')
    cd /Users/u/WebKit/OpenSource/Tools/DumpRenderTree
    export ACTION\=build
    /bin/sh -c /Users/u/Build/DumpRenderTree.build/Debug/Derived\\\ Sources.build/Script-0F18E7011D6B9CC60027E547.sh
Generating bindings for UIScriptController...
ruby /Users/u/Build/Debug/usr/local/include/wtf/Scripts/GeneratePreferences.rb --frontend WebKitLegacy --base /Users/u/Build/Debug/usr/local/include/wtf/Scripts/Preferences/WebPreferences.yaml --debug /Users/u/Build/Debug/usr/local/include/wtf/Scripts/Preferences/WebPreferencesDebug.yaml --experimental /Users/u/Build/Debug/usr/local/include/wtf/Scripts/Preferences/WebPreferencesExperimental.yaml --internal /Users/u/Build/Debug/usr/local/include/wtf/Scripts/Preferences/WebPreferencesInternal.yaml --template /Users/u/WebKit/OpenSource/Tools/DumpRenderTree/Scripts/PreferencesTemplates/TestOptionsGeneratedWebKitLegacyKeyMapping.cpp.erb
perl -I /Users/u/Build/Debug/WebCore.framework/Versions/A/PrivateHeaders -I /Users/u/WebKit/OpenSource/Tools/DumpRenderTree/../TestRunnerShared/UIScriptContext/Bindings -I /Users/u/WebKit/OpenSource/Tools/DumpRenderTree/Bindings /Users/u/Build/Debug/WebCore.framework/Versions/A/PrivateHeaders/generate-bindings.pl --defines "WTF_PLATFORM_COCOA WTF_PLATFORM_MAC" --include /Users/u/WebKit/OpenSource/Tools/DumpRenderTree/../TestRunnerShared/UIScriptContext/Bindings --outputDir . --generator DumpRenderTree --idlAttributesFile /Users/u/Build/Debug/WebCore.framework/Versions/A/PrivateHeaders/IDLAttributes.json /Users/u/WebKit/OpenSource/Tools/DumpRenderTree/../TestRunnerShared/UIScriptContext/Bindings/UIScriptController.idl
ruby /Users/u/Build/Debug/usr/local/include/wtf/Scripts/GeneratePreferences.rb --frontend WebKitLegacy --base /Users/u/Build/Debug/usr/local/include/wtf/Scripts/Preferences/WebPreferences.yaml --debug /Users/u/Build/Debug/usr/local/include/wtf/Scripts/Preferences/WebPreferencesDebug.yaml --experimental /Users/u/Build/Debug/usr/local/include/wtf/Scripts/Preferences/WebPreferencesExperimental.yaml --internal /Users/u/Build/Debug/usr/local/include/wtf/Scripts/Preferences/WebPreferencesInternal.yaml --template /Users/u/WebKit/OpenSource/Tools/DumpRenderTree/Scripts/PreferencesTemplates/TestOptionsGeneratedKeys.h.erb
PhaseScriptExecution endtest
END
for my $i (0..scalar(@generatePrefsLines) - 1) {
    my $previousLine = $i ? $generatePrefsLines[$i - 1] : "";
    my $line = $generatePrefsLines[$i];
    if ($line =~ /GeneratePreferences.rb|PhaseScriptExecution|Generating bindings/) {
        is(shouldIgnoreLine($previousLine, $line), 0, description("Printed: " . $line));
    } else {
        is(shouldIgnoreLine($previousLine, $line), 1, description("Ignored: " . $line));
    }
}

done_testing();

sub description($)
{
    my ($line) = @_;

    my $maxLineLength = 200;
    my $ellipsis = "...";
    my $truncateLength = $maxLineLength - length($ellipsis);

    my $description = length($line) > $maxLineLength ? substr($line, 0, $truncateLength) : $line;
    $description .= $ellipsis if length($line) != length($description);

    return $description;
}
