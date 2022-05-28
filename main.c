
#include <stdio.h>
#include <sys/socket.h>
#include <string.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <time.h>
#include <assert.h>
#include <sys/time.h>
#include <netdb.h>
#include "rfc6234/sha.h"




int make_socket(){
    int status; //Variable Importante pour tester les appels Systeme
    //On  crée une socket polymorphe
    int s =socket(AF_INET6, SOCK_DGRAM, 0);
    if(s < 0){
        perror("Erreur lors de la création de la socket ");
        exit(-3);
    }
    int val = 0;
    status=setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &val, sizeof(val));//Permet de rendre la  socket polymorphe
    if(status<0){
        perror("Erreur lors de setcokopt");
        exit(-2);
    }
    val=1;
    status=setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val));// Pour réutiliser le port
    if(status<0){
        perror("Erreur lors de setcokopt");
        exit(-2);
    }

    //Puis on crée une struct addrinfo pour pouvoir utiliser getaddrinfo

    struct sockaddr_in6 server; // On bind notre serveur
    memset(&server,0,sizeof(server));
    server.sin6_family=AF_INET6;
    server.sin6_port=htons(1818);
    status=bind(s,(struct sockaddr*) &server, sizeof(server));
    if (status<0){
        perror(" Erreur lors de bind");
        exit(-3);
    }
    return s;

}

void create_req(char *paquet,short len){
    paquet[0]=95; // Permet de créer l'entete de la requete
    paquet[1]=1;
    len=htons(len);
    memmove(paquet+2,&len,2);
}

typedef struct Voisin Voisin;
struct Voisin{ // La structure de donée Voisin comme défini dans le rapport
    struct in6_addr IP;
    int permanent;
    short port;
    struct timeval lastreceive;
};


int premier_voisin(Voisin *tb_voisin,char *adrr,char *port ){// Permet de recupéreru  l'adresse du premier voisin et de l'ajouter
    int status;
    struct addrinfo hints,*t;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET6;//Ipv6
    hints.ai_socktype = SOCK_DGRAM;//Datagramme car nous somme en UDP
    hints.ai_flags = AI_V4MAPPED | AI_ALL;//Permet d'utiliser Ipv6 et Ipv4-mapped
    hints.ai_protocol = 0;  //Permet a getaddrinfo() de renvoyer des adresses de socket de n'importe quel type
    if ((status = getaddrinfo(adrr, port, &hints, &t))!= 0) {// Getaddrinfo retourne 0 en cas de succés
        perror("Erreur lors de getaddrinfo");
        exit(-3);
    }
    struct addrinfo *parcours;
    int nb_voisins=0;
    struct sockaddr_in6 *adrr_jch;// L'adresse pour ce connecte se trouve dans adrr_jch
    for (parcours=t;parcours!=NULL;parcours=parcours->ai_next){
        adrr_jch=(struct sockaddr_in6 *)parcours->ai_addr;
        tb_voisin[nb_voisins].permanent=1;
        tb_voisin[nb_voisins].port=adrr_jch->sin6_port;
        tb_voisin[nb_voisins].IP=adrr_jch->sin6_addr; // On recupère nos premiers voisins
        nb_voisins+=1;
    }
    freeaddrinfo(t);
    return nb_voisins;

}



typedef struct Data Data; // on definit les donnee d'un noeud (ou pair) qui represente un le triple (id,sequence,message)
struct Data
{
    uint64_t id;
    unsigned short sequence; //Pour etre sur d'avoir un entier entre 0 et 65 535(2^16 -1) codé sur 16 bits car int depend du procésseur
    char chaine[192]; //192 est la longeur maximale
    int len_chaine;
    int myself;
};

int nb_octets_noeud(Data data){//8 pour l'id ,2 pour le short code sur 16 bytes et le reste est la taille des données
    return 10+strlen(data.chaine);
}

int modif_mymessage(char *message,int len,Data *tb_data,int nb_data){// Permert de modifier le message de la data modif
    for (int i =0;i<nb_data;i++){// Renvoie 1 en cas de succèes et -1 en cas d'echec
        if(tb_data[i].myself==1){
            memset(tb_data[i].chaine,0,192);
            memcpy(tb_data[i].chaine,message,len);// On copie en profondeur notre nouveau message
            tb_data[i].len_chaine=len;
            tb_data[i].sequence=(tb_data[i].sequence+1)%65536;// On augmente la séquence
            return 1;
        }
    }
    return -1;
}

int ajoute_data(Data *tb_data,Data new_data,int nb_data,int max_data){// On ajoute une data au tableau de donnees
    if (nb_data>=max_data-1){
        int max_data2=nb_data+(max_data)/2;
        Data new_tb_data[max_data2];
        if(new_tb_data!=NULL){ // Permet de réalouer de la mémoire quand la table de donnée est saturé
            for (int i=0;i<nb_data;i++){
                memcpy(&new_tb_data[i].id,&tb_data[i].id,8);
                memcpy(&new_tb_data[i].sequence,&tb_data[i].sequence,2);
                memcpy(new_tb_data[i].chaine,tb_data[i].chaine,tb_data[i].len_chaine);
                memcpy(&new_tb_data[i].myself,&tb_data[i].myself,4);
                memcpy(&new_tb_data[i].len_chaine,&tb_data[i].chaine,4);
            }
            tb_data=new_tb_data;
            memcpy(&max_data,&max_data2,4);
            return  ajoute_data(tb_data,new_data,nb_data,max_data);
        }
        return nb_data;
    }
    if(nb_data==0){ // On ajoute le premier noeud
        memcpy(&tb_data[0].id,&new_data.id,8);
        memcpy(&tb_data[0].sequence,&new_data.sequence,2);
        memcpy(tb_data[0].chaine,new_data.chaine,new_data.len_chaine);
        memcpy(&tb_data[0].myself,&new_data.myself,4);
        memcpy(&tb_data[0].len_chaine,&new_data.len_chaine,4);
        return  nb_data+1;
    }
    int i;// Ici on ajoute dans un tableau trié, on décale les elements de un a droite
    for (i=nb_data; (i>0)&&memcmp(&tb_data[i-1].id,&new_data.id,8);i--){//On trie par rapport au octets donc memcmp
        memcpy(&tb_data[i].id,&tb_data[i-1].id,8);
        memcpy(&tb_data[i].sequence,&tb_data[i-1].sequence,2);// On decale les data au bon endroit
        memcpy(tb_data[i].chaine,tb_data[i-1].chaine,tb_data[i-1].len_chaine);
        memcpy(&tb_data[i].len_chaine,&tb_data[i-1].len_chaine,4);
        memcpy(&tb_data[i].myself,&tb_data[i-1].myself,4);
    }
    memcpy(&tb_data[i].id,&new_data.id,8); // On ajoute la nouvelle donné a sa place
    memcpy(&tb_data[i].sequence,&new_data.sequence,2);
    memcpy(&tb_data[i].len_chaine,&new_data.len_chaine,4);
    memcpy(&tb_data[i].myself,&new_data.myself,4);
    memcpy(&tb_data[i].chaine,&new_data.chaine,new_data.len_chaine);
    return nb_data+1;
}


int h( unsigned char *suite_octets ,int len, unsigned char *res){ // La fonction h décrite dans le poly du projet
    SHA256Context ctx; // Elle retourne-1 en cas d'echec et 0 en cas de succès
    unsigned char hash[32];
    int status;
    status=SHA256Reset(&ctx);
    if(status< 0){
        perror("Erreur lors de la fonction SHA256Reset");
        return -1;
    }
    status=SHA256Input(&ctx,suite_octets,len);
    if(status< 0){
        perror("Erreur lors de la fonction SHA256Input");
        return -1;
    }
    status=SHA256Result(&ctx,hash);
    if(status!= 0){
        perror("Erreur lors de la fonction SHA256Result");
        return -1;
    }
    memmove(res,&hash,16); //On tronque les 16 premiers octets
    return 0;
    // Provient d'un mail de Julius
}


int h_noeud(Data data,unsigned char *res){//Elle retourne-1 en cas d'echec et 0 en cas de succès
    int len_concat=nb_octets_noeud(data); // Permet de renvoyer le hash du noeud selon la fonction h
    int len_chaine=len_concat-10;
    unsigned char concan[len_concat];
    memmove(&concan,&data.id,8);
    memmove(concan+8,&data.sequence,2);
    memmove(concan+10,&data.chaine,len_chaine); // On concatene les octets
    int status=h(concan,len_concat,res);
    if (status==0){
        return 0;
    }
    return -1;
}

int h_reseau(Data *tb_data,int len,unsigned char *res){//Elle retourne-1 en cas d'echec et 0 en cas de succès
    unsigned long long nb_octets =16*len;
    unsigned char *has=malloc(nb_octets);// Peut échouer quand le nombre devient très grand
    if (has == NULL){
        printf ("Erreur lors de Malloc \n");
        return -1;
    }
     // Cette fonction renvoie le hash du réseau selon la fonction h
    int pos_tab=0;
    for(int i=0;i<len;i++){
        unsigned char hashnd[16];
        h_noeud(tb_data[i],hashnd);
        memcpy(has+pos_tab,hashnd,16);
        pos_tab+=16;
    }
    int status=h(has,nb_octets,res);
    free(has);
    if (status==0){
        return 0;
    }
    return -1;
}

uint64_t create_id() { //Unsigned long long est un entier de 8 octets
    u_int64_t id;
    unsigned char i[8]; // Ici on génère 8 octets de facon aléatoire (Pas si aléatoire hahaha)
    i[0]=27;// Date anniversaire de greg
    i[1]=5; // 27 mai donc
    i[2]=15; // Date anniversaire de Thibault
    i[3]=9;  // 15 septembre donc
    i[4]=20; // Note au projet
    i[5]=187;// Chiffre au hasard
    i[6]=99; //Annee de naissance
    i[7]=61; // Jour passé en confinement
    memcpy(&id,i,8);
    return id;
}

int paquet_is_correct(char *paquet,int octets_lu){ // Renvoie 1 si le paquet est correcte (selon son header) ou -1 sinon
    if (octets_lu<4){ //Si le nombre d'octets lu est inferieur a 5 on ne peut essayer de parser le paquet
        return -1;
    }
    short length;
    memmove(&length,paquet+2,2);
    length=ntohs(length);//Boutisme
    if (paquet[0]==95 && paquet[1]==1 && length>0 && octets_lu>=length+4){//On regarde si on ignore le paquet ou pas
        return 1;
    }
    return -1;
}

short length_body_paquet (char *paquet,int octets_lu){// Renvoie la taille du paquet selon son header
    if (paquet_is_correct(paquet,octets_lu)==1){
        short length;
        memmove(&length,paquet+2,2);
        length=ntohs(length);
        if (length>1020){//Si body_length est superieur a 1020 on renvoie 1020 d'apres le sujet
            return 1020;
        }
        return length; // Sinon on return body_length
    }
    return -1;
}

int type_TLV(char *paquet,int debut_TLV){ // Renvoie le type du TLV présent a cette endroit (paquet+debut_TLV)
    unsigned char type=paquet[debut_TLV];
    return type;
}

int next_TLV(char *paquet,short body_length,int debut_TLV){ // Renvoie le prochain TLV
    if (debut_TLV>=body_length){  //Il n'y a pas de prochain TLV a lire
        return -1;
    }
    char type=paquet[debut_TLV];
    if(type==0){// Seul TLV qui n'a pas de len
        return debut_TLV+1;
    }
    if(body_length>debut_TLV+1){// Permet de savoir si on peut recup la taille du paquet
        unsigned char taille=paquet[debut_TLV+1];// On recupere la taille
        if(body_length>=debut_TLV+2+taille){
            return debut_TLV+2+taille;
        }
        return -1;
    }
    return -1;
}


int make_TLV0(char *paquet,int octets_occupes){//Return -1 si le TLV de type 0 n'a pas était ajoute au paquet
    if (octets_occupes<1024){ //Sinon ajoute le TLV0 a la requete et return le nouveau nombre d'octets occupés
        paquet[octets_occupes]=0; // On indique le TYPE =0
        return octets_occupes+1;
    }
    return -1;
}

int make_TLV1(char *paquet,int octets_occupes,int len){//Return -1 si le TLV de type 1 n'a pas était ajoute au paquet
    if ((octets_occupes+2+len)<1025){//Sinon ajoute le TLV1 a la requete et return le nouveau nombre d'octets occupés
        paquet[octets_occupes]=1; // On indique le TYPE =1
        paquet[octets_occupes+1]=len;// ON indique le length
        memset(paquet+octets_occupes+2,0,len); // On ajoute les 0 de longeur len
        return octets_occupes+2+len;
    }
    return -1;
}

int make_TLV2(char *paquet,int octets_occupes){ //Return -1 si le TLV de type 2 n'a pas était ajoute au paquet
    if (octets_occupes<1023){ //Sinon ajoute le TLV2 a la requete et return le nouveau nombre d'octets occupés.
        //memset(paquet+octets_occupes,2,1); // On indique le TYPE =2
        //memset(paquet+octets_occupes+1,0,1);// On met length a 0 comme indiqué
        paquet[octets_occupes]=2;
        paquet[octets_occupes+1]=0;
        return octets_occupes+2;
    }
    return -1;
}

int make_TLV3(char *paquet,int octets_occupes,Voisin voisin){//Return -1 si le TLV de type 3 n'a pas était ajoute au paquet
    if (octets_occupes+20<1025){//Sinon ajoute le TLV3 a la requete et return le nouveau nombre d'octets occupés.
        paquet[octets_occupes]=3; // On indique le TYPE =3
        paquet[octets_occupes+1]=18;// On met length a 18 car 16 pour l'ip et 2 pour le port
        memmove(paquet+octets_occupes+2,&voisin.IP,16);//On  ajoute les 16 octets qui composent l'adresse IP
        //voisin.port=htons(voisin.port);
        memmove(paquet+octets_occupes+18,&voisin.port,2); // On ajoute les 2 octets du port
        return octets_occupes+20;
    }
    return -1;
}

int make_TLV4(char *paquet,int octets_occupes,Data *tb_data,int nb_data){//Return -1 si le TLV de type 4 n'a pas était ajoute au paquet
    unsigned char hash[16];

    if(h_reseau(tb_data,nb_data,hash)==0){
        if (octets_occupes+18<1025) {//Sinon ajoute le TLV4 a la requete et return le nouveau nombre d'octets occupés.
            paquet[octets_occupes]=4; // On indique le TYPE =4
            paquet[octets_occupes+1]=16;// On met length a 16 car 16 pour le hash du reseau
            memmove(paquet+octets_occupes+2,hash,16);
            return octets_occupes+18;
        }
        return -1;
    }
    return octets_occupes;
}

int make_TLV5(char *paquet,int octets_occupes){ //Return -1 si le TLV de type 5 n'a pas était ajoute au paquet
    if (octets_occupes<1023){ //Sinon ajoute le TLV5 a la requete et return le nouveau nombre d'octets occupés.
        paquet[octets_occupes]=5; // On indique le TYPE =5
        paquet[octets_occupes+1]=0;// On met length a 0 comme indiqué
        return octets_occupes+2;
    }
    return -1;
}

int make_TLV6(char *paquet,int octets_occupes,Data data){//Return -1 si le TLV de type 6 n'a pas était ajoute au paquet
    if (octets_occupes+28<1025) {//Sinon ajoute le TLV6 a la requete et return le nouveau nombre d'octets occupés.
        paquet[octets_occupes]=6; // On indique le TYPE =6
        paquet[octets_occupes+1]=26;// On met length a 26 car 16 pour le hash du noeud,8 pour l'id et 2 pour seqno
        memmove(paquet+octets_occupes+2,&data.id,8);// On met l'id
        data.sequence=ntohs( data.sequence);
        memmove(paquet+octets_occupes+10,&data.sequence,2); // Puis on met le seqno
        unsigned char res[16];
        if (h_noeud(data,res)==0){
            memmove(paquet+octets_occupes+12,res,16);// On ajoute le h_noeud
            return octets_occupes+28;
        }
        else{
            perror("Erreur lors de h_noeud");
            return octets_occupes;
        }
    }
    return -1;
}

int make_TLV7(char *paquet,int octets_occupes,Data data) {//Return -1 si le TLV de type 7 n'a pas était ajoute au paquet
    if (octets_occupes+10<1025) {//Sinon ajoute le TLV6 a la requete et return le nouveau nombre d'octets occupés
        paquet[octets_occupes]=7; // On indique le TYPE =7
        paquet[octets_occupes+1]=8;// On met length a 8 car 8 pour l'id
        memmove(paquet+octets_occupes+2,&data.id,8);// On met l'id
        return octets_occupes+10;
    }
    return -1;
}

int make_TLV8(char *paquet,int octets_occupes,Data data){//Return -1 si le TLV de type 8 n'a pas était ajoute au paquet
    if ((octets_occupes+28+strlen(data.chaine))<1025 && strlen(data.chaine)<193) {//Sinon ajoute le TLV8 a la requete et return le nouveau nombre d'octets occupés.
        paquet[octets_occupes]=8; // On indique le TYPE =8
        paquet[octets_occupes+1]=26+strlen(data.chaine);// On met length a 26 car 16 pour le hash du noeud,8 pour l'id et 2 pour seqno
        memmove(paquet+octets_occupes+2,&data.id,8);// On met l'id
        data.sequence=ntohs( data.sequence);
        memmove(paquet+octets_occupes+10,&data.sequence,2); // Puis on met le seqno
        unsigned char res[16];
        if (h_noeud(data,res)==0){
            memmove(paquet+octets_occupes+12,res,16);//On ajoute le hachage du noeud
            memmove(paquet+octets_occupes+28,data.chaine,strlen(data.chaine));// On ajoute les data du noeud
            return octets_occupes+28+strlen(data.chaine);
        }
        else{
            perror("Erreur lors de h_noeud\n");
            return octets_occupes;
        }
    }
    return -1;
}



int  tri_tb_voisin(Voisin *tb_voisin,int nb_voisin){//Return le nouveau nombre de voisin
    Voisin res[nb_voisin]; //On cree un tableau intermédiaire
    int voisin_garde=0;
    struct timeval now;
    gettimeofday(&now, NULL);// On prend la date la plus proche possible
    for(int i=0;i<nb_voisin;i++){
        if((now.tv_sec-tb_voisin[i].lastreceive.tv_sec)<70 || tb_voisin[i].permanent==1){// On regarde si le noeud et permanent ou si le dernier message date de moins de 70s
            // Si c'est le cas on copie en profondeur ce noeud,sinon on ne fais rien
            memset(&res[voisin_garde],0, sizeof(Voisin));
            memcpy(&res[voisin_garde].permanent,&tb_voisin[i].permanent,4);
            memcpy(&res[voisin_garde].port,&tb_voisin[i].port,2);
            memcpy(&res[voisin_garde].IP,&tb_voisin[i].IP,16);
            memcpy(&res[voisin_garde].lastreceive.tv_sec,&tb_voisin[i].lastreceive.tv_sec,4);
            memcpy(&res[voisin_garde].lastreceive.tv_usec,&tb_voisin[i].lastreceive.tv_usec,4);
            voisin_garde+=1;
        }
    }
    //Ici res contient le tableau correct de tous les noeuds
    //Pour des soucis de simpliciter on va copier res sur tb_voisin
    for(int i=0;i<nb_voisin;i++){
        memset(&tb_voisin[i],0, sizeof(Voisin));//On remet a 0 tout les noeud
        if(i<voisin_garde){// Puis on re remplie le tableau apres modifications
            memcpy(&tb_voisin[i].permanent,&res[i].permanent,4);
            memcpy(&tb_voisin[i].port,&res[i].port,2);
            memcpy(&tb_voisin[i].IP,&res[i].IP,16);
            memcpy(&tb_voisin[i].lastreceive.tv_sec,&res[i].lastreceive.tv_sec,4);
            memcpy(&tb_voisin[i].lastreceive.tv_usec,&res[i].lastreceive.tv_usec,4);
        }
    }
    return voisin_garde;
}

int demande_voisin(Voisin *tb_voisin,int nb_voisin,int socket){
    int octets_occupes=4;
    char req[1024];
    struct sockaddr_in6 help;
    if (nb_voisin<5 && nb_voisin>0){ // Si le nombre de voisin est inferieur a 5 on envoie un TLV Neighbour Request à un voisin tiré au hasard
        int random=rand()%nb_voisin;
        octets_occupes=make_TLV2(req,octets_occupes);
        memset(&help,0, sizeof(help));
        memcpy(&help.sin6_addr,&tb_voisin[random].IP,16);// On recupere l'adresse IP
        memcpy(&help.sin6_port,&tb_voisin[random].port,2); // On recuperer le port ;
        // help.sin6_port=htons(help.sin6_port);
        help.sin6_family=AF_INET6;
        create_req(req,octets_occupes-4);
        int kk=sendto(socket,req,octets_occupes,0,(struct sockaddr *) &help, sizeof(help));
        if (kk>0) {
            //printf("La table des voisins contenait moins de 5 elements ont envoie donc un TLV 2 a un voisin au hasard\n");

        }        else {
            printf("erreur lors de l'nvoi de neig req \n");
        }
        return 1;
    }
    return -1;
}


int maj_tb_voisin(Voisin *tb_voisin,int nb_voisin,struct sockaddr_in6 from){// Fonction pour voir si on ajoute from comme nouveau voisin
    unsigned short port=from.sin6_port;
    from.sin6_port=ntohs(from.sin6_port);
    for (int i=0;i<nb_voisin;i++){// Update le voisin si il est deja present
        if( memcmp(&from.sin6_addr,&tb_voisin[i].IP,16)==0){//le noeud est deja un voisin
            struct timeval now; // On met a jour le dernier paquet recu
            gettimeofday(&now,NULL);
            memcpy(&tb_voisin[i].lastreceive.tv_sec,&now.tv_sec,4);
            memcpy(&tb_voisin[i].lastreceive.tv_usec,&now.tv_usec,4);
            return nb_voisin; // On return le nouveau nombre de voisin
        }
    }
    if(nb_voisin<15){// Si le nombre de voisin est inférieur a 15 on l'ajoute a la table
        memset(&tb_voisin[nb_voisin],0,sizeof(Voisin));
        tb_voisin[nb_voisin].permanent=0;
        struct timeval now; // On met a jour le dernier paquet recu
        gettimeofday(&now,NULL);
        memcpy(&tb_voisin[nb_voisin].lastreceive.tv_sec,&now.tv_sec,4);
        memcpy(&tb_voisin[nb_voisin].lastreceive.tv_usec,&now.tv_usec,4);
        memcpy(&tb_voisin[nb_voisin].port,&port,2);
        memcpy(&tb_voisin[nb_voisin].IP,&from.sin6_addr,16);
        // printf("Le noeud ne fait pas partie de la table des voisins , on l'ajoute donc\n");
        return nb_voisin+1;
    }
    //printf("Le noeud ne fait pas partie de la table des voisins et elle contient deja 15 voisins\n");
    return nb_voisin;

}

void state_network(Voisin *tb_voisin,int nb_voisin,int socket,Data *tb_data,int nb_data){// Envoie un TLV4 a tous nos voisins
    int octets_occupes=4;
    char req[1024];
    octets_occupes=make_TLV4(req,octets_occupes,tb_data,nb_data);
    create_req(req,octets_occupes-4);
    for(int i=0;i<nb_voisin;i++){
        struct sockaddr_in6 help;
        memset(&help,0, sizeof(help));
        memcpy(&help.sin6_addr,&tb_voisin[i].IP,16);// On recupere l'adresse IP
        memcpy(&help.sin6_port,&tb_voisin[i].port,2); // On recuperer le port ;
        // help.sin6_port=htons(help.sin6_port);
        help.sin6_family=AF_INET6;
        sendto(socket,req,octets_occupes,0,(struct sockaddr *) &help, sizeof(help));
    }
}

void sendMessage(Voisin *tb_voisin,int nb_voisin,int socket,Data *tb_data,int nb_data){
    char message[193];// 192 Pour enlever le caractere de saut de ligne (ENTER)
    fgets(message, 193, stdin);// On recupere le message sur l'entrée standard
    int len=strlen(message)-1;
    message[len]='\0';
    if(strncmp(message,"quit",strlen("quit"))==0 && strlen("quit")==strlen(message)){// Si on veut quitter le programme
        printf("Vous avez quitté le programme,Au revoir à bientot \n");
        exit(-1);
    }
    printf("Le message que tu as rentré est :%s\n", message);
    modif_mymessage(message,len,tb_data,nb_data);// On modifie notre message dans la table de donnée
    int indice;
    for (int i=0;i<nb_data;i++){
        if(tb_data[i].myself==1){
            indice=i;
        }
    }
    int octets_occupes=4;
    char req[1024];
    if(octets_occupes==make_TLV8(req,octets_occupes,tb_data[indice])){// En cas d'echec de h_noeud.
        return ;
    }
    octets_occupes=octets_occupes+28+strlen(tb_data[indice].chaine);// Ici je peux utiliser strlen car j'entre moi meme la chaine avec \0
    create_req(req,octets_occupes-4);
    for(int i=0;i<nb_voisin;i++){
        struct sockaddr_in6 help;
        memset(&help,0, sizeof(help));
        memcpy(&help.sin6_addr,&tb_voisin[i].IP,16);// On recupere l'adresse IP
        memcpy(&help.sin6_port,&tb_voisin[i].port,2); // On recuperer le port ;
        help.sin6_family=AF_INET6;
        sendto(socket,req,octets_occupes,0,(struct sockaddr *) &help, sizeof(help));
    }
    printf("On a envoyer un message a tous nos voisins\n");
}

void answer_TLV2(Voisin *tb_voisin,int nb_voisin,struct sockaddr_in6 from,int socket){ // Reponse quand on recoit un TLV2
    char envoie[1024];
    int rand;
    int taille=4;
    int status;
    rand=random()%nb_voisin;
    taille=make_TLV3(envoie,taille,tb_voisin[rand]);// On envoie un TLV 3 a un voisin au hasard
    create_req(envoie,taille-4);
    status=sendto(socket,envoie,taille,0,(struct sockaddr *) &from, sizeof(from));

    if (status<0){
        printf("Une erreur s'est produite dans la fonction answer_TLV2\n");
    }

}

void answer_TLV3(char * req,int debut_tlv,int s,Data *tb_data,int nb_data){  // Reponse quand on recoit un TLV3
    char envoie[1024];
    int taille=4;
    int status;
    taille=make_TLV4(envoie,taille,tb_data,nb_data); // On envoie un TLV4 a l'adresse recu
    create_req(envoie,taille-4);
    struct sockaddr_in6 help;
    memset(&help,0, sizeof(help));
    memcpy(&help.sin6_addr,req+debut_tlv+2,16);// On recupere l'adresse IP
    memcpy(&help.sin6_port,req+debut_tlv+18,2); // On recuperer le port ;
    help.sin6_port=ntohs(help.sin6_port);
    help.sin6_port=htons(help.sin6_port);
    help.sin6_family=AF_INET6;
    status=sendto(s,envoie,taille,0,(struct sockaddr *) &help, sizeof(help));

    if (status<0){
        printf("Une erreur s'est produite dans la fonction answer_TLV3\n");
    }

}

void answer_TLV4(char * req,int debut_tlv,int socket,struct sockaddr_in6 from,Data *tb_data,int nb_data){// Reponse quand on recoit un TLV4
    unsigned char hash[16];
    h_reseau(tb_data,nb_data,hash);
    if(memcmp(hash,req+debut_tlv+2,16)!=0){// Si les hash sont différents on envoie un TLV5
        char envoie[1024];
        int taille=4;
        int status;
        taille=make_TLV5(envoie,taille);
        create_req(envoie,taille-4);
        status=sendto(socket,envoie,taille,0,(struct sockaddr *) &from, sizeof(from));
        if (status<0){
            printf("Une erreur s'est produite dans la fonction answer_TLV4\n");
        }

    }

}

void answer_TLV5(int socket,struct sockaddr_in6 from,Data *tb_data,int nb_data) {// Reponse quand on recoit un TLV5
    char envoie[1024];
    int taille = 4;
    for (int i = 0; i < nb_data; i++) {// Ici on envoie les TLV 6 par série
        if (taille + 28 > 1024) {
            create_req(envoie,taille-4);
            sendto(socket, envoie, taille, 0,(struct sockaddr *) &from, sizeof(from));
            memset(envoie, 0, 1024);
            taille = 4;
        }
        taille=make_TLV6(envoie,taille,tb_data[i]);
    }
    if(taille>4) {
        create_req(envoie,taille-4);
        sendto(socket, envoie, taille, 0,(struct sockaddr *) &from, sizeof(from));
    }
}

void answer_TLV6(char * req,int socket,struct sockaddr_in6 from,Data *tb_data,int nb_data,int debut_tlv){// Reponse quand on recoit un TLV6
    for (int i=0;i<nb_data;i++){
        if(memcmp(&tb_data[i].id,req+2+debut_tlv,8)==0){
            unsigned char hash[16];
            h_noeud(tb_data[i],hash);
            if(memcmp(hash,req+debut_tlv+12,16)==0){// Si les hashes sont égaux on ne fait rien
                return;
            }
            char envoie[1024];
            int taille = 4;
            taille=make_TLV7(envoie,taille,tb_data[i]);// Sinon on envoie un TLV7
            create_req(envoie,taille-4);
            sendto(socket,envoie,taille,0,(struct sockaddr *) &from, sizeof(from));
            //printf("On a recu un TLV 6 on répond par un TLV 7\n");
            return;
        }
    }
    char envoie[1024];
    int taille = 4;
    Data data;
    memcpy(&data.id,req+2+debut_tlv,8);
    taille=make_TLV7(envoie,taille,data);
    create_req(envoie,taille-4);
    sendto(socket,envoie,taille,0,(struct sockaddr *) &from, sizeof(from));
    //printf("On a recu un TLV 6 on répond par un TLV 7\n");
    return;
}

void answer_TLV7(char * req,int socket,struct sockaddr_in6 from,Data *tb_data,int nb_data,int debut_tlv) {// Reponse quand on recoit un TLV7
    for (int i = 0; i < nb_data; i++) {
        if (memcmp(&tb_data[i].id, req + 2 + debut_tlv, 8) == 0) {
            char envoie[1024];
            int taille = 4;
            taille = make_TLV8(envoie, taille, tb_data[i]);// On envoie un TLV 8 du noeud demandé
            create_req(envoie, taille - 4);
            sendto(socket, envoie, taille, 0,(struct sockaddr *) &from, sizeof(from));
            //printf("On a recu un TLV 7 on répond par un TLV 8\n");
        }
        return;
    }
}
int answer_TLV8(char * req,int socket,struct sockaddr_in6 from,Data *tb_data,int nb_data,int debut_tlv,int max_data){
    unsigned char len=req[debut_tlv+1];
    int len_message=len-26;
    if(len<26){
        return nb_data;
    }
    for (int i=0;i<nb_data;i++){
        if (memcmp(&tb_data[i].id, req + 2 + debut_tlv, 8) == 0 && tb_data[i].myself==1){
            unsigned short sequence;
            memcpy(&sequence,req + 10 + debut_tlv,2);
            sequence=ntohs(sequence);
            if(((tb_data[i].sequence-sequence) &32768 )!=0){// Si c'est notre noeud on augmente seulement le numero de sequence si nécessaire
                tb_data[i].sequence=(sequence+1)%65536;
            }
            return nb_data;
        }
        else if (memcmp(&tb_data[i].id, req + 2 + debut_tlv, 8) == 0){
            short sequence;
            memcpy(&sequence,req + 10 + debut_tlv,2);
            sequence=ntohs(sequence);
            if(((sequence - tb_data[i].sequence) &32768) ==0){ //Le noeud est dans la table et on a un message plus récent
                memset(&tb_data[i],0, sizeof(Data));
                memcpy(&tb_data[i].id,req + 2 + debut_tlv,8);// On update donc sa data
                memcpy(&tb_data[i].sequence,req + 10 + debut_tlv,2);
                tb_data[i].sequence=ntohs(tb_data[i].sequence);
                tb_data[i].len_chaine=len_message;
                memcpy(tb_data[i].chaine,req+debut_tlv+26,len_message);
            }
            return nb_data;
        }
    }
    Data data; // Le noeud n'est pas dans la table on l'ajoute donc
    memset(&data,0, sizeof(data));
    memcpy(&data.id,req + 2 + debut_tlv,8);
    memcpy(&data.sequence,req + 10 + debut_tlv,2);
    data.sequence=ntohs(data.sequence);
    data.myself=0;
    memcpy(&data.len_chaine,&len_message,4);
    memcpy(data.chaine,req+debut_tlv+26,len_message);
    nb_data=ajoute_data(tb_data,data,nb_data,max_data);
    return nb_data;
}

int parse_req(char * req,unsigned short  taille,int socket ,struct sockaddr_in6 from,Voisin *tb_voisin,int nb_voisin,Data *tb_data,int nb_data,int max_data){
    int debut_tlv=4; // Fonction qui parse la requete en fonction des TLV
    if (taille<5){
        debut_tlv=-1;// On regarde le type et on utilise la fonction AnswerTLV en fonction
    }
    else {
        debut_tlv=4;
    }
    while(debut_tlv!=-1 && debut_tlv<1021){
        if(type_TLV(req,debut_tlv)==2) {
            answer_TLV2(tb_voisin,nb_voisin,from,socket);
        }

        else if(type_TLV(req,debut_tlv)==3) {
            answer_TLV3(req,debut_tlv,socket,tb_data,nb_data);
        }

        else if(type_TLV(req,debut_tlv)==4) {
            answer_TLV4(req,debut_tlv,socket,from,tb_data,nb_data);
        }

        else if(type_TLV(req,debut_tlv)==5) {
            answer_TLV5(socket,from,tb_data,nb_data);
        }

        else if(type_TLV(req,debut_tlv)==6) {
            answer_TLV6(req,socket,from,tb_data,nb_data,debut_tlv);
        }

        else if(type_TLV(req,debut_tlv)==7) {
            answer_TLV7(req,socket,from,tb_data,nb_data,debut_tlv);
        }

        else if(type_TLV(req,debut_tlv)==8) {
            nb_data=answer_TLV8(req,socket,from,tb_data,nb_data,debut_tlv,max_data);
        }

        else if(type_TLV(req,debut_tlv)==9) {// Ne rien faire
            printf("%s\n",req+debut_tlv+2);// On affiche un TLV qui indique une erreur
            exit(-1);
        }
        debut_tlv=next_TLV(req,taille-4,debut_tlv);
    }
    return nb_data;
}

void actuMyData(int socket,Voisin *tb_voisin,int nb_voisin,Data *tb_data,int nb_data){
    int indice=0;
    for(int i=0;i<nb_data;i++){
        if(tb_data[i].myself==1){
            indice=i;
        }
    }
    char req[1024];// Cette fonction permet de garder a jour notre donnée pour que l'envoie des message soit plus fluide.
    int taille=4;
    taille=make_TLV7(req,taille,tb_data[indice]);
    create_req(req,taille-4);
    for(int i=0;i<nb_voisin;i++){
        struct sockaddr_in6 help;
        memset(&help,0, sizeof(help));
        memcpy(&help.sin6_addr,&tb_voisin[i].IP,16);// On recupere l'adresse IP
        memcpy(&help.sin6_port,&tb_voisin[i].port,2); // On recuperer le port ;
        help.sin6_family=AF_INET6;
        sendto(socket,req,taille,0,(struct sockaddr *) &help, sizeof(help));
    }
    return;
}

int main(int argc, char *argv[]) {
    assert( argc == 3 ); // Si le NomDuDomaine et le numéro de port ne sont pas présent on quitte le programme
    int max_data=5000;
    int nb_data=0;
    Data tb_data[5000]; // On cree la table de donnée
    for(int i=0;i<5000;i++){// On initialise a 0 la variable
        memset(&tb_data[i],0,sizeof(Data));
    }

    int nb_voisin=0;
    Voisin tb_voisin[15];//On cree la table des voisins
    for(int i=0;i<15;i++){// On initialise a 0 la variable
        memset(&tb_voisin[i],0,sizeof(Voisin));
    }

    nb_voisin=premier_voisin(tb_voisin,argv[1],argv[2]);//On rentre le premier voisin
    int s=make_socket();// On cree notre socket polymorphe et on la bind a notre port


    struct timeval now,now_20;// Pour connaitre quand 20 seconde se sont écoulés
    gettimeofday(&now,NULL);
    gettimeofday(&now_20,NULL);
    srand(time(NULL)); // Pour pouvoir utiliser random dans le programme


    // On s'ajoute nous meme a la table des données et jch a la table des voisins
    Data myself;
    myself.sequence=0;
    myself.id=create_id();
    myself.len_chaine=0;
    myself.myself=1;
    nb_data=ajoute_data(tb_data,myself,nb_data,2500);




    printf ("Pour écrire des messages il suffit d'écrire sur la sortie standard .\nLes message de plus de 192 charactère seront tronqués au 192ème charactère.\n");
    printf("Pour quitter le programme , rentrer 'quit'\n");
    for(;;){// Boucle principal
        char reception[1024]; //Nombre d'octets maximum pour un datagramme en UDP
        // Reception pour recupérer les paquets et envoie pour les envoyer
        struct sockaddr_in6 from;// Permet de connaitre le destinataire des paquets
        socklen_t len6 = sizeof(from);
        memset(&from,0,len6);

        struct sockaddr_in6 help;// Pour envoyer divers paquet
        memset(&help,0,len6);
        int octets_lu;
        struct timeval tv = {0, 10000};
        fd_set readfs;
        FD_ZERO (&readfs);
        FD_SET (s, &readfs);
        FD_SET (fileno(stdin), &readfs);
        int err = select (FD_SETSIZE, &readfs, NULL, NULL, &tv);
        if(err<0){
            perror("Erreur lors de l'appel select");
            exit(-3);
        }



        if (FD_ISSET (fileno(stdin), &readfs)) { // Quand l'utilisateur veut envoyer un message
            sendMessage(tb_voisin,nb_voisin,s,tb_data,nb_data);

        }


        if (FD_ISSET (s, &readfs)){ // Quand on recoit un paquet
            octets_lu=recvfrom(s,reception,1024,0,(struct sockaddr *) &from, &len6);// On recupere le message dans la socket
            if(octets_lu<0){
                perror("erreur lors de recvfrom");
                exit(-2);
            }
            if (paquet_is_correct(reception,octets_lu)==1){// On verifie si le paquet est correcte sinon on ignore
                unsigned short body_length=length_body_paquet(reception,octets_lu);
                nb_voisin=maj_tb_voisin(tb_voisin,nb_voisin,from);
                nb_data=parse_req(reception,body_length,s,from,tb_voisin,nb_voisin,tb_data,nb_data,max_data);
            }

        }

        actuMyData(s,tb_voisin,nb_voisin,tb_data,nb_data);

        if(now.tv_sec-now_20.tv_sec>20) {// On regarde si 20 seconde se sont ecoulés
            nb_voisin=tri_tb_voisin(tb_voisin,nb_voisin);
            demande_voisin(tb_voisin,nb_voisin,s);
            state_network(tb_voisin,nb_voisin,s,tb_data,nb_data);
            gettimeofday(&now_20, NULL);
        }
        gettimeofday(&now, NULL);
    }
    return 0;

}


