/* Real backend → player resume → pause/exit → portable repository.
 * The stream uses the anthology's S4 coordinate; cloud identity stays S1. */
#include "player.h"
#include "video.h"
#include "catalogo.h"
#include "progresso.h"
#include "perfis.h"
#include "dados.h"
#include "seriealias.h"
#include "ajustes.h"
#include "gfx.h"
#include "text.h"
#include "tex_cache.h"
#include <SDL2/SDL_image.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
static void tick(void){SDL_PumpEvents();player_atualizar(.005f,SDL_GetTicks());SDL_Delay(5);}
int main(int argc,char **argv){
 assert(argc==2);SDL_SetHint("SDL_MAC_BACKGROUND_APP","1");
 assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)==0);IMG_Init(IMG_INIT_PNG|IMG_INIT_JPG);
 SDL_Window *w=SDL_CreateWindow("watch playback test",0,0,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);assert(w);
 SDL_GLContext gl=SDL_GL_CreateContext(w);assert(gl);gfx_tamanho_alvo(1920,1080);assert(gfx_iniciar());
 assert(txt_iniciar("deploy/app",1));tex_iniciar(64);dados_iniciar(NULL);ajustes_dir(dados_dir());player_dir(dados_dir());perfis_definir_ativo(1);
 ProgRegistro remote={.perfil=1,.temporada=1,.episodio=1,.posSeg=30,.durSeg=0,.percentual=0,.lastWatchedMs=1700000000000};
 snprintf(remote.contentId,sizeof remote.contentId,"tmdb:299939");strcpy(remote.tipo,"series");strcpy(remote.titulo,"Monstro: A História de Lizzie Borden");strcpy(remote.poster,"lizzie.jpg");
 assert(prog_aplicar_remoto(&remote));
 CatItem *item=calloc(1,sizeof *item);assert(item);strcpy(item->imdb,remote.contentId);strcpy(item->tipo,remote.tipo);strcpy(item->titulo,remote.titulo);strcpy(item->poster,remote.poster);
 item->temporada=item->episodio=1;item->posicaoSeg=30;item->continuarGerenciado=1;seriealias_aplicar(item);cat_definir(item,1);free(item);
 player_abrir(0,argv[1]);player_definir_episodio(4,1);
 Uint64 deadline=SDL_GetTicks64()+7000;
 while(SDL_GetTicks64()<deadline && (!video_pronto() || video_pos()<30))tick();
 assert(video_pronto() && video_pos()>=30 && video_pos()<32);
 int t=0,e=0;player_episodio_atual(&t,&e);assert(t==4&&e==1);
 video_buscar(45);for(int i=0;i<80;i++)tick();video_pausar(1);for(int i=0;i<50;i++)tick();
 ProgRegistro saved;assert(prog_por_chave("tmdb:299939_s1e1",&saved));
 assert(saved.temporada==1&&saved.episodio==1&&saved.posSeg>=45&&saved.posSeg<47&&saved.durSeg>119);
 assert(!strcmp(saved.contentId,"tmdb:299939")&&!strcmp(saved.poster,"lizzie.jpg")&&saved.pendente);
 double position=saved.posSeg;player_encerrar();prog_invalidar();assert(prog_por_chave("tmdb:299939_s1e1",&saved));assert(fabs(saved.posSeg-position)<.1);
 player_abrir(0,argv[1]);player_definir_episodio(4,1);deadline=SDL_GetTicks64()+7000;
 while(SDL_GetTicks64()<deadline && (!video_pronto() || video_pos()<position-.1))tick();
 assert(video_pronto()&&fabs(video_pos()-position)<1);
 video_pausar(1);player_encerrar();
 puts("PASS: actual player resumes unknown-duration remote state, saves pause/exit, reloads exact position and preserves Lizzie identity/art + S1 cloud coordinates");
 return 0;
}
