"""Route remaining user-visible literals through the existing language table."""
from pathlib import Path
def apply(root):
 def patch(name,old,new):
  p=Path(root)/name;s=p.read_text(encoding='utf8')
  if s.count(old)!=1:raise ValueError('Localization patch context: '+name+': '+old)
  p.write_text(s.replace(old,new),encoding='utf8')
 patch('Lawn/Widget/AwardScreen.cpp','aAchievementName.append(" Earned!");',
       'aAchievementName.append(mApp->GetString("ACHIEVEMENT_EARNED_SUFFIX", " Earned!"));')
 patch('Lawn/Widget/ZombatarWidget.cpp','g->DrawString("Delete?", ZOMBATAR_LIST_DELETE_X, ZOMBATAR_LIST_DELETE_Y);',
       'g->DrawString(mApp->GetString("ZOMBATAR_DELETE_BUTTON_LABEL", "Delete?"), ZOMBATAR_LIST_DELETE_X, ZOMBATAR_LIST_DELETE_Y);')
 patch('Lawn/Widget/SeedChooserScreen.cpp','mRandomButton->SetLabel("(Debug Play)");',
       'mRandomButton->SetLabel(mApp->GetString("RANDOM_SELECTION_BUTTON", "(Debug Play)"));')
 patch('Lawn/MessageWidget.h','\tMessageWidget(LawnApp* theApp);',
       '\tMessageWidget(LawnApp* theApp);\n\tvoid H1LocalizeRestoredText();')
 patch('Lawn/MessageWidget.cpp','#include <algorithm>',
       '#include <algorithm>\n#include "localization/LegacyMessageMap.h"')
 patch('Lawn/MessageWidget.cpp','void MessageWidget::SetLabel(std::string_view theNewLabel, MessageStyle theMessageStyle)',r'''// Save4 stores already-expanded labels. Normalize only once after loading;
// ordinary updates and draws do no extra lookup work.
void MessageWidget::H1LocalizeRestoredText()
{
    auto translate=[this](char* label) {
        for(const auto& entry:H1LegacyMessages) {
            if(std::string_view(label)!=entry.english)continue;
            auto value=mApp->GetString(entry.key,entry.english);
            if(value==entry.english)return false;
            std::string next(value);TruncateLabel(next);
            SDL_strlcpy(label,next.c_str(),MAX_MESSAGE_LENGTH);
            return true;
        }
        return false;
    };
    const bool changed=translate(mLabel);
    translate(mLabelNext);
    if(changed && mReanimType!=ReanimationType::REANIM_NONE) {
        const int savedSlideOffTime=mSlideOffTime;
        ClearReanim();LayoutReanimText();mSlideOffTime=savedSlideOffTime;
    }
}

void MessageWidget::SetLabel(std::string_view theNewLabel, MessageStyle theMessageStyle)''')
 patch('Lawn/System/SaveGame.cpp','\ttheBoard->mAdvice->mApp = theBoard->mApp;',
       '\ttheBoard->mAdvice->mApp = theBoard->mApp;\n\ttheBoard->mAdvice->H1LocalizeRestoredText();')
 patch('SexyAppFramework/graphics/ImageFont.h','class FontData;\n',
       'class FontData;\nstruct H1CJKFontTable;\n')
 patch('SexyAppFramework/graphics/ImageFont.h','\tbool\t\t\t\t\tmUseAlphaCorrection;\n\npublic:\n\tFontLayer',
       '\tbool\t\t\t\t\tmUseAlphaCorrection;\n\tstd::shared_ptr<H1CJKFontTable> mH1CJKFont;\n\tint mH1CJKOffsetY=0;\n\npublic:\n\tFontLayer')
 patch('SexyAppFramework/graphics/ImageFont.cpp','using namespace Sexy;',
       '#include "localization/CompactFont.h"\n\nusing namespace Sexy;')
 patch('SexyAppFramework/graphics/ImageFont.cpp','mUseAlphaCorrection(theFontLayer.mUseAlphaCorrection)',
       'mUseAlphaCorrection(theFontLayer.mUseAlphaCorrection),\n\tmH1CJKFont(theFontLayer.mH1CJKFont),mH1CJKOffsetY(theFontLayer.mH1CJKOffsetY)')
 patch('SexyAppFramework/graphics/ImageFont.cpp','anItr = mCharDataMap.insert(CharDataMap::value_type(theChar, CharData())).first;',
       'CharData glyph;H1CJKLookup(*this,theChar,glyph);\n\t\tanItr = mCharDataMap.insert(CharDataMap::value_type(theChar, glyph)).first;')
 patch('SexyAppFramework/graphics/ImageFont.cpp','\tif (strcasecmp(aCmd.c_str(), "Define") == 0)',r'''    if (strcasecmp(aCmd.c_str(), "LayerSetH1CJK") == 0) {
        FontLayer* layer=nullptr;int offset=0;
        if(theParams.mElementVector.size()!=4||!DataToLayer(theParams.mElementVector[1].get(),&layer)||theParams.mElementVector[2]->mIsList||theParams.mElementVector[3]->mIsList)return Error("Invalid compact CJK font parameters");
        std::string file;if(!DataToString(theParams.mElementVector[2].get(),&file))return Error("Invalid compact CJK table path");
        if(!StringToInt(static_cast<SingleDataElement*>(theParams.mElementVector[3].get())->mString,&offset))return Error("Invalid compact CJK baseline");
        auto table=H1LoadCJKTable(mApp,file);if(!table)return Error("Invalid compact CJK font table");
        for(const auto& glyph:table->glyphs)if(glyph.x+glyph.w>uint32_t(layer->mImage->GetWidth())||glyph.y+glyph.h>uint32_t(layer->mImage->GetHeight()))return Error("Compact CJK rectangle out of bounds");
        layer->mH1CJKFont=std::move(table);layer->mH1CJKOffsetY=offset;return true;
    }
    if (strcasecmp(aCmd.c_str(), "Define") == 0)''')
 patch('SexyAppFramework/graphics/ImageFont.cpp','\t\t\t\t\t// Resize font elements',
       '\t\t\t\t\tH1CJKPopulateForScaledFont(*aFontLayer);\n\t\t\t\t\t// Resize font elements')
 patch('SexyAppFramework/graphics/ImageFont.cpp','\t\t\tRect& aScaledCharRect = anActiveFontLayer->mScaledCharImageRects[aChar];',
       '\t\t\tif(aScale==1.0 && anActiveFontLayer->mBaseFontLayer->mH1CJKFont)anActiveFontLayer->mScaledCharImageRects.try_emplace(aChar,aCharData->mImageRect);\n\t\t\tRect& aScaledCharRect = anActiveFontLayer->mScaledCharImageRects[aChar];')
