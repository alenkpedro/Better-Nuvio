// Navegacao superior: perfil a esquerda, capsula central expansiva,
// ajustes e relogio a direita.
//
// Ele nao e uma tela: e uma camada que aparece POR CIMA do conteudo e toma o
// D-pad enquanto esta visivel. Por isso a API foge do padrao de tela em dois
// pontos, de proposito:
//   - nao tem menu_encerrar: texto e icones usam os caches de text.c e gfx.c;
//     o modulo nao possui alocacoes independentes.
//   - nao tem menu_quer_sair: fechar o menu nunca fecha o app. O Back aqui
//     devolve o foco ao conteudo, e quem decide sair continua sendo a home.
//
// Na Home, CIMA no destaque entra na barra; ESQUERDA/DIREITA percorrem os
// destinos e o avatar, OK escolhe e BAIXO devolve foco ao conteudo.
#ifndef NV_MENU_H
#define NV_MENU_H
#include <SDL2/SDL.h>

// IDs estaveis dos destinos. Ajustes tem icone proprio a direita; o avatar
// abre a lista de perfis. IDs legados ficam para chamadas internas.
typedef enum {
  MENU_INICIO,
  MENU_EXPLORAR,
  MENU_GUIA,
  MENU_BUSCAR,
  MENU_BIBLIOTECA,
  // Agenda fica depois da Biblioteca na barra visivel.
  MENU_AGENDA,
  MENU_PERFIL,
  MENU_AJUSTES,
  MENU_DESCUBRIR,
  MENU_N
} MenuDestino;

// Zera destino e animacao. So e necessario se o app reinicializar a UI.
int  menu_iniciar(void);

// Expande a barra e entrega o foco a ela. Chamar ja aberta nao faz nada.
void menu_abrir(void);
// Fecha sem escolher: o destaque volta para o destino atual.
void menu_fechar(void);

// 1 enquanto a barra e dona do D-pad. OK do controle devolve foco ao conteudo;
// com mouse, a capsula permanece aberta ate o cursor sair dela.
int  menu_aberto(void);
// Os destinos compactos permanecem visiveis nas telas principais.
int  menu_visivel(void);

// Destino em vigor (um MenuDestino). menu_definir_destino existe para o app
// impor o estado inicial ou reagir a uma navegacao que nao veio da barra.
int  menu_destino(void);
void menu_definir_destino(int destino);
// 1 uma unica vez, no quadro em que o usuario escolheu um destino DIFERENTE do
// que estava em vigor. Consome a flag: quem le, trata. Sem isso o app teria que
// guardar o destino anterior so para descobrir que ele mudou.
int  menu_mudou_destino(void);
// Selecionar outro perfil no menu devolve slot+1 (zero = nenhum pedido).
int  menu_pediu_trocar_perfil(void);
int  menu_pediu_gerenciar_perfis(void);

const char *menu_rotulo(int destino);

void menu_evento(const SDL_Event *e);
void menu_atualizar(float dt, Uint32 agora);
// Desenhe por ultimo: a barra flutua sobre o conteudo.
void menu_desenhar(Uint32 agora);

#endif
