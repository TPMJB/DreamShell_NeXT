#ifndef LAUNCHER_HOST_TSU_H
#define LAUNCHER_HOST_TSU_H
#include <ds.h>
Texture *TSU_TextureCreateFromFile(const char *,bool,bool,unsigned int);
void TSU_TextureDestroy(Texture **);
int TSU_TextureGetW(Texture *);
int TSU_TextureGetH(Texture *);
Banner *TSU_BannerCreate(int,Texture *);
void TSU_BannerDestroy(Banner **);
void TSU_BannerSetSize(Banner *,float,float);
void TSU_BannerSetTexture(Banner *,Texture *);
void TSU_AppSubAddBanner(DSApp *,Banner *);
void TSU_AppSubRemoveBanner(DSApp *,Banner *);
Label *TSU_LabelCreate(Font *,const char *,int,bool,bool,bool);
void TSU_LabelDestroy(Label **);
void TSU_LabelSetText(Label *,const char *);
void TSU_LabelGetSize(Label *,float *,float *);
void TSU_LabelSetTint(Label *,const Color *);
void TSU_AppSubAddLabel(DSApp *,Label *);
void TSU_AppSubRemoveLabel(DSApp *,Label *);
void TSU_DrawableSetTranslate(Drawable *,const Vector *);
void TSU_DrawableSetAlpha(Drawable *,float);
void TSU_AppSetDrawTransparentPolyEvent(DSApp *,void (*)(void));
void TSU_DialogHide(Dialog *);
void TSU_DialogShow(Dialog *,const char *);
int TSU_DialogIsVisible(Dialog *);
void TSU_DialogSetFocus(Dialog *,int);
void TSU_DialogMoveFocus(Dialog *,int);
void TSU_DialogActivateFocused(Dialog *);
void TSU_DialogHandleClick(Dialog *,int,int);
#endif
