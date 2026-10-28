#pragma once
#include <windows.h>
#include <string>
#include <cstring>

enum class LocaleId {
    EN_UK = 0,
    EN_US,
    FR_BE,
    JA,
    PT_BR,
    PT_PT
};

struct LocaleStrings {
    const wchar_t* brandName;
    const wchar_t* productName;
    const wchar_t* loadingAccount;
    const wchar_t* loadingDetails;
    const wchar_t* signIn;
    const wchar_t* createAccount;
    const wchar_t* signingIn;
    const wchar_t* opening;
    const wchar_t* signOut;
    const wchar_t* retry;
    const wchar_t* cancel;
    const wchar_t* close;
    const wchar_t* back;
    const wchar_t* ecosystemLink;

    const wchar_t* tabAccount;
    const wchar_t* tabQuests;
    const wchar_t* tabSnowflakes;

    const wchar_t* snowflakesSubtitle;
    const wchar_t* snowflakeInputLabel;
    const wchar_t* snowflakeInputTitle;
    const wchar_t* snowflakePlaceholder;
    const wchar_t* snowflakeInputHint;
    const wchar_t* snowflakeInvalid;
    const wchar_t* snowflakeCreatedAt;
    const wchar_t* snowflakeDetailsTitle;
    const wchar_t* snowflakeRawId;
    const wchar_t* snowflakeUnixMs;
    const wchar_t* snowflakeTimestampBits;
    const wchar_t* snowflakeWorkerId;
    const wchar_t* snowflakeProcessId;
    const wchar_t* snowflakeIncrement;
    const wchar_t* snowflakeCopy;
    const wchar_t* snowflakeCopied;
    const wchar_t* snowflakeIdentityTitle;

    const wchar_t* yourAccount;
    const wchar_t* signInPrompt;
    const wchar_t* identifierPlaceholder;
    const wchar_t* passwordPlaceholder;
    const wchar_t* discordTokenMissing;
    const wchar_t* discordTokenInvalid;

    const wchar_t* discordAccountTitle;
    const wchar_t* userIdLabel;
    const wchar_t* creationDateLabel;
    const wchar_t* nixlabsProfileTitle;
    const wchar_t* emailLabel;
    const wchar_t* standingGood;
    const wchar_t* standingLimited;
    const wchar_t* standingBanned;
    const wchar_t* languageLabel;
    const wchar_t* roleLabel;

    const wchar_t* questsTitle;
    const wchar_t* questsSubtitle;
    const wchar_t* noQuestsFound;
    const wchar_t* noQuestsDescription;
    const wchar_t* completeAll;
    const wchar_t* completeQuest;
    const wchar_t* refreshQuests;
    const wchar_t* questCompleted;
    const wchar_t* running;
    const wchar_t* liveProgressTitle;
    const wchar_t* stopQuest;
    const wchar_t* loadMoreQuests;

    const wchar_t* connectDiscord;
    const wchar_t* discordNotConnected;
    const wchar_t* discordConnectPrompt;
    const wchar_t* discordLoginTabCredentials;
    const wchar_t* discordLoginTabToken;
    const wchar_t* discordEmailLabel;
    const wchar_t* discordPasswordLabel;
    const wchar_t* discordTokenInputLabel;
    const wchar_t* discordTokenInputPlaceholder;
    const wchar_t* discordTokenInputHint;
    const wchar_t* discordSaveTokenBtn;
    const wchar_t* discordDisconnect;
    const wchar_t* manageInMyAccount;
};

inline const LocaleStrings kLocales[] = {
    // EN_UK
    {
        L"nixlabs", L"Discord Helper", L"Loading account\x2026", L"Loading details\x2026",
        L"Sign in", L"Create account", L"Signing in\x2026", L"Opening\x2026", L"Sign out",
        L"Retry", L"Cancel", L"Close", L"Back", L"nixlabs.tech",
        L"Account", L"Quests", L"Snowflakes",
        L"Decode any Discord snowflake into its creation time and raw ID fields",
        L"Snowflake ID", L"Paste a snowflake", L"Paste a Discord snowflake ID",
        L"Digits only, up to 64 bits. Decoded locally from the ID.",
        L"Enter a valid Discord snowflake from the past.",
        L"Created at", L"Raw ID details", L"Raw ID", L"Unix timestamp (ms)",
        L"Timestamp bits (ms since epoch)", L"Worker ID", L"Process ID", L"Increment",
        L"Copy", L"Copied", L"Discord identity",
        L"Your account", L"Enter your nixlabs credentials to continue",
        L"Username, handle, or email", L"Password or PIN",
        L"No Discord token saved. Add one in My Account to use Discord Helper.",
        L"Your Discord token is invalid. Replace it in My Account.",
        L"Discord Account", L"User ID", L"Account created", L"Nixlabs Profile",
        L"Email address", L"Good standing", L"Limited standing", L"Suspended / Banned",
        L"Language", L"Tier",
        L"Discord Quests", L"Discover and complete active Discord promotional quests",
        L"No Quests", L"There are currently no active completable quests for this account. Check back later!",
        L"Complete all available quests", L"Complete quest", L"Refresh list",
        L"Completed", L"Running\x2026", L"Current Task", L"Stop", L"Load more",
        L"Connect Discord", L"No Discord account connected",
        L"Connect your Discord account to complete and track quests.",
        L"Sign In", L"Token", L"Email or phone number", L"Password",
        L"Discord Token", L"Paste your Discord user token",
        L"Your token is encrypted with AES-256-GCM in Nixlabs Accounts.",
        L"Save & Connect", L"Disconnect", L"Manage token in My Account"
    },
    // EN_US
    {
        L"nixlabs", L"Discord Helper", L"Loading account\x2026", L"Loading details\x2026",
        L"Sign in", L"Create account", L"Signing in\x2026", L"Opening\x2026", L"Sign out",
        L"Retry", L"Cancel", L"Close", L"Back", L"nixlabs.tech",
        L"Account", L"Quests", L"Snowflakes",
        L"Decode any Discord snowflake into its creation time and raw ID fields",
        L"Snowflake ID", L"Paste a snowflake", L"Paste a Discord snowflake ID",
        L"Digits only, up to 64 bits. Decoded locally from the ID.",
        L"Enter a valid Discord snowflake from the past.",
        L"Created at", L"Raw ID details", L"Raw ID", L"Unix timestamp (ms)",
        L"Timestamp bits (ms since epoch)", L"Worker ID", L"Process ID", L"Increment",
        L"Copy", L"Copied", L"Discord identity",
        L"Your account", L"Enter your nixlabs credentials to continue",
        L"Username, handle, or email", L"Password or PIN",
        L"No Discord token saved. Add one in My Account to use Discord Helper.",
        L"Your Discord token is invalid. Replace it in My Account.",
        L"Discord Account", L"User ID", L"Account created", L"Nixlabs Profile",
        L"Email address", L"Good standing", L"Limited standing", L"Suspended / Banned",
        L"Language", L"Tier",
        L"Discord Quests", L"Discover and complete active Discord promotional quests",
        L"No Quests", L"There are currently no active completable quests for this account. Check back later!",
        L"Complete all available quests", L"Complete quest", L"Refresh list",
        L"Completed", L"Running\x2026", L"Current Task", L"Stop", L"Load more",
        L"Connect Discord", L"No Discord account connected",
        L"Connect your Discord account to complete and track quests.",
        L"Sign In", L"Token", L"Email or phone number", L"Password",
        L"Discord Token", L"Paste your Discord user token",
        L"Your token is encrypted with AES-256-GCM in Nixlabs Accounts.",
        L"Save & Connect", L"Disconnect", L"Manage token in My Account"
    },
    // FR_BE
    {
        L"nixlabs", L"Discord Helper", L"Chargement du compte\x2026", L"Chargement des d\x00e9tails\x2026",
        L"Se connecter", L"Cr\x00e9er un compte", L"Connexion en cours\x2026", L"Ouverture\x2026", L"Se d\x00e9connecter",
        L"R\x00e9essayer", L"Annuler", L"Fermer", L"Retour", L"nixlabs.tech",
        L"Compte", L"Qu\x00eates", L"Snowflakes",
        L"D\x00e9codez la date de cr\x00e9ation et les champs bruts d\x2019un identifiant Discord",
        L"Identifiant Snowflake", L"Coller un identifiant", L"Collez un identifiant Snowflake Discord",
        L"Chiffres uniquement, sur 64 bits au maximum. D\x00e9codage local.",
        L"Entrez un identifiant Snowflake Discord valide datant du pass\x00e9.",
        L"Cr\x00e9\x00e9 le", L"D\x00e9tails bruts de l\x2019identifiant", L"Identifiant brut", L"Horodatage Unix (ms)",
        L"Bits d\x2019horodatage (ms depuis l\x2019\x00e9poque)", L"ID du serveur", L"ID du processus", L"Incr\x00e9ment",
        L"Copier", L"Copi\x00e9", L"Identit\x00e9 Discord",
        L"Votre compte", L"Entrez vos identifiants nixlabs pour continuer",
        L"Nom d\x2019utilisateur, identifiant ou e-mail", L"Mot de passe ou code PIN",
        L"Aucun jeton Discord enregistr\x00e9. Ajoutez-en un dans Mon compte.",
        L"Votre jeton Discord est invalide. Remplacez-le dans Mon compte.",
        L"Compte Discord", L"Identifiant utilisateur", L"Compte cr\x00e9\x00e9 le", L"Profil Nixlabs",
        L"Adresse e-mail", L"En r\x00e8gle", L"Statut limit\x00e9", L"Suspendu / Banni",
        L"Langue", L"Niveau",
        L"Qu\x00eates Discord", L"D\x00e9couvrez et compl\x00e9tez les qu\x00eates promotionnelles Discord actives",
        L"Aucune qu\x00eate", L"Il n\x2019y a actuellement aucune qu\x00eate disponible pour ce compte.",
        L"Compl\x00e9ter toutes les qu\x00eates disponibles", L"Compl\x00e9ter la qu\x00eate", L"Actualiser la liste",
        L"Compl\x00e9t\x00e9e", L"En cours\x2026", L"T\x00e2che en cours", L"Arr\x00eater", L"Charger plus",
        L"Connecter Discord", L"Aucun compte Discord li\x00e9",
        L"Connectez votre compte Discord pour suivre et accomplir des qu\x00eates.",
        L"Connexion directe", L"Jeton", L"E-mail ou T\x00e9l\x00e9phone", L"Mot de passe",
        L"Jeton utilisateur Discord", L"Collez votre jeton utilisateur Discord",
        L"Votre jeton est chiffr\x00e9 en AES-256-GCM dans Nixlabs Accounts.",
        L"Enregistrer et connecter", L"D\x00e9connecter", L"G\x00e9rer le jeton dans Mon compte"
    },
    // JA
    {
        L"nixlabs", L"Discord Helper", L"\x30a2\x30ab\x30a6\x30f3\x30c8\x3092\x8aad\x307f\x8fbc\x307f\x4e2d\x2026", L"\x8a73\x7d30\x3092\x8aad\x307f\x8fbc\x307f\x4e2d\x2026",
        L"\x30b5\x30a4\x30f3\x30a4\x30f3", L"\x30a2\x30ab\x30a6\x30f3\x30c8\x4f5c\x6210", L"\x30b5\x30a4\x30f3\x30a4\x30f3\x4e2d\x2026", L"\x958b\x3044\x3066\x3044\x307e\x3059\x2026", L"\x30b5\x30a4\x30f3\x30a2\x30a6\x30c8",
        L"\x518d\x8a66\x884c", L"\x30ad\x30e3\x30f3\x30bb\x30eb", L"\x9589\x3058\x308b", L"\x623b\x308b", L"nixlabs.tech",
        L"\x30a2\x30ab\x30a6\x30f3\x30c8", L"\x30af\x30a8\x30b9\x30c8", L"\x30b9\x30ce\x30fc\x30d5\x30ec\x30fc\x30af",
        L"Discord \x306e\x30b9\x30ce\x30fc\x30d5\x30ec\x30fc\x30af\x304b\x3089\x4f5c\x6210\x65e5\x6642\x3068 ID \x306e\x8a73\x7d30\x3092\x8868\x793a",
        L"\x30b9\x30ce\x30fc\x30d5\x30ec\x30fc\x30af ID", L"\x30b9\x30ce\x30fc\x30d5\x30ec\x30fc\x30af\x3092\x8cbc\x308a\x4ed8\x3051", L"Discord \x306e\x30b9\x30ce\x30fc\x30d5\x30ec\x30fc\x30af ID \x3092\x8cbc\x308a\x4ed8\x3051",
        L"\x6570\x5b57\x306e\x307f\x3001\x6700\x5927 64 \x30d3\x30c3\x30c8\x3002ID \x304b\x3089\x30ed\x30fc\x30ab\x30eb\x3067\x89e3\x6790\x3057\x307e\x3059\x3002",
        L"\x904e\x53bb\x306e\x6709\x52b9\x306a Discord \x30b9\x30ce\x30fc\x30d5\x30ec\x30fc\x30af\x3092\x5165\x529b\x3057\x3066\x304f\x3060\x3055\x3044\x3002",
        L"\x4f5c\x6210\x65e5\x6642", L"ID \x306e\x8a73\x7d30", L"\x5143\x306e ID", L"Unix \x30bf\x30a4\x30e0\x30b9\x30bf\x30f3\x30d7 (ms)",
        L"\x30bf\x30a4\x30e0\x30b9\x30bf\x30f3\x30d7\x30d3\x30c3\x30c8", L"\x30ef\x30fc\x30ab\x30fc ID", L"\x30d7\x30ed\x30bb\x30b9 ID", L"\x9023\x756a",
        L"\x30b3\x30d4\x30fc", L"\x30b3\x30d4\x30fc\x3057\x307e\x3057\x305f", L"Discord \x306e\x8b58\x5225\x60c5\x5831",
        L"\x30a2\x30ab\x30a6\x30f3\x30c8", L"\x7d9a\x884c\x3059\x308b\x306b\x306f nixlabs \x306e\x8cc7\x683c\x60c5\x5831\x3092\x5165\x529b\x3057\x3066\x304f\x3060\x3055\x3044",
        L"\x30e6\x30fc\x30b6\x30fc\x540d\x3001\x30cf\x30f3\x30c9\x30eb\x3001\x307e\x305f\x306f\x30e1\x30fc\x30eb", L"\x30d1\x30b9\x30ef\x30fc\x30c9\x307e\x305f\x306f PIN",
        L"Discord \x30c8\x30fc\x30af\x30f3\x304c\x4fdd\x5b58\x3055\x308c\x3066\x3044\x307e\x305b\x3093\x3002",
        L"Discord \x30c8\x30fc\x30af\x30f3\x304c\x7121\x52b9\x3067\x3059\x3002",
        L"Discord\x30a2\x30ab\x30a6\x30f3\x30c8", L"\x30e6\x30fc\x30b6\x30fcID", L"\x30a2\x30ab\x30a6\x30f3\x30c8\x4f5c\x6210\x65e5", L"Nixlabs\x30d7\x30ed\x30d5\x30a3\x30fc\x30eb",
        L"\x30e1\x30fc\x30eb\x30a2\x30c9\x30ec\x30b9", L"\x826f\x597d\x306a\x30b9\x30c6\x30fc\x30bf\x30b9", L"\x5236\x9650\x3055\x308c\x305f\x30b9\x30c6\x30fc\x30bf\x30b9", L"\x505c\x6b62 / \x5229\x7528\x7981\x6b62",
        L"\x8a00\x8a9e", L"\x30c6\x30a3\x30a2",
        L"Discord\x30af\x30a8\x30b9\x30c8", L"\x5229\x7528\x53ef\x80fd\x306a" L"Discord\x30d7\x30ed\x30e2\x30fc\x30b7\x30e7\x30f3\x30af\x30a8\x30b9\x30c8\x3092\x81ea\x52d5\x9054\x6210",
        L"\x30af\x30a8\x30b9\x30c8\x306f\x3042\x308a\x307e\x305b\x3093", L"\x73fe\x5728\x3053\x306e\x30a2\x30ab\x30a6\x30f3\x30c8\x3067\x9054\x6210\x53ef\x80fd\x306a\x30af\x30a8\x30b9\x30c8\x306f\x3042\x308a\x307e\x305b\x3093\x3002",
        L"\x5229\x7528\x53ef\x80fd\x306a\x3059\x3079\x3066\x306e\x30af\x30a8\x30b9\x30c8\x3092\x9054\x6210", L"\x30af\x30a8\x30b9\x30c8\x3092\x9054\x6210", L"\x4e00\x89a7\x3092\x66f4\x65b0",
        L"\x9054\x6210\x6e08\x307f", L"\x5b9f\x884c\x4e2d\x2026", L"\x9032\x884c\x4e2d\x306e\x30bf\x30b9\x30af", L"\x505c\x6b62", L"\x3055\x3089\x306b\x8aad\x307f\x8fbc\x3080",
        L"Discord\x3092\x9023\x643a", L"Discord\x30a2\x30ab\x30a6\x30f3\x30c8\x304c\x672a\x9023\x643a\x3067\x3059",
        L"\x30af\x30a8\x30b9\x30c8\x3092\x81ea\x52d5\x9054\x6210\x30fb\x7ba1\x7406\x3059\x308b\x306b\x306f\x3001" L"Discord\x30a2\x30ab\x30a6\x30f3\x30c8\x3092\x9023\x643a\x3057\x3066\x304f\x3060\x3055\x3044\x3002",
        L"\x76f4\x63a5\x30ed\x30b0\x30a4\x30f3", L"\x30c8\x30fc\x30af\x30f3", L"\x30e1\x30fc\x30eb\x30a2\x30c9\x30ec\x30b9\x307e\x305f\x306f\x96fb\x8a71\x756a\x53f7", L"\x30d1\x30b9\x30ef\x30fc\x30c9",
        L"Discord \x30e6\x30fc\x30b6\x30fc\x30c8\x30fc\x30af\x30f3", L"\x30e6\x30fc\x30b6\x30fc\x30c8\x30fc\x30af\x30f3\x3092\x8cbc\x308a\x4ed8\x3051",
        L"\x30c8\x30fc\x30af\x30f3\x306f AES-256-GCM \x3067\x5b89\x5168\x306b\x6697\x53f7\x5316\x3055\x308c\x307e\x3059\x3002",
        L"\x4fdd\x5b58\x3057\x3066\x9023\x643a", L"\x9023\x643a\x89e3\x9664", L"\x30de\x30a4\x30a2\x30ab\x30a6\x30f3\x30c8\x3067\x30c8\x30fc\x30af\x30f3\x3092\x7ba1\x7406"
    },
    // PT_BR
    {
        L"nixlabs", L"Discord Helper", L"Carregando conta\x2026", L"Carregando detalhes\x2026",
        L"Entrar", L"Criar conta", L"Entrando\x2026", L"Abrindo\x2026", L"Sair",
        L"Tentar novamente", L"Cancelar", L"Fechar", L"Voltar", L"nixlabs.tech",
        L"Conta", L"Miss\x00f5es", L"Snowflakes",
        L"Decodifique a data de cria\x00e7\x00e3o e os campos brutos de qualquer ID do Discord",
        L"ID Snowflake", L"Cole um snowflake", L"Cole um ID Snowflake do Discord",
        L"Apenas d\x00edgitos, at\x00e9 64 bits. Decodificado localmente.",
        L"Insira um snowflake v\x00e1lido do Discord do passado.",
        L"Criado em", L"Detalhes brutos do ID", L"ID bruto", L"Timestamp Unix (ms)",
        L"Bits do timestamp (ms desde a \x00e9poca)", L"ID do servidor", L"ID do processo", L"Incremento",
        L"Copiar", L"Copiado", L"Identidade Discord",
        L"Sua conta", L"Digite suas credenciais nixlabs para continuar",
        L"Nome de usu\x00e1rio, handle ou e-mail", L"Senha ou PIN",
        L"Nenhum token do Discord foi salvo. Adicione um em Minha Conta.",
        L"Seu token do Discord \x00e9 inv\x00e1lido. Substitua-o em Minha Conta.",
        L"Conta do Discord", L"ID do usu\x00e1rio", L"Conta criada em", L"Perfil Nixlabs",
        L"Endere\x00e7o de e-mail", L"Boa reputa\x00e7\x00e3o", L"Reputa\x00e7\x00e3o limitada", L"Suspenso / Banido",
        L"Idioma", L"N\x00edvel",
        L"Miss\x00f5es do Discord", L"Descubra e complete miss\x00f5es promocionais ativas do Discord",
        L"Sem miss\x00f5es", L"No momento n\x00e3o h\x00e1 miss\x00f5es ativas dispon\x00edveis para esta conta.",
        L"Completar todas as miss\x00f5es dispon\x00edveis", L"Completar miss\x00e3o", L"Atualizar lista",
        L"Conclu\x00edda", L"Em andamento\x2026", L"Tarefa atual", L"Parar", L"Carregar mais",
        L"Conectar Discord", L"Nenhuma conta do Discord vinculada",
        L"Conecte sua conta do Discord para concluir e acompanhar miss\x00f5es.",
        L"Login Direto", L"Token", L"E-mail ou Telefone", L"Senha",
        L"Token de Usu\x00e1rio do Discord", L"Cole seu token do Discord",
        L"Seu token \x00e9 criptografado com AES-256-GCM no Nixlabs Accounts.",
        L"Salvar e Conectar", L"Desconectar", L"Gerenciar token no Meu Perfil"
    },
    // PT_PT
    {
        L"nixlabs", L"Discord Helper", L"A carregar conta\x2026", L"A carregar detalhes\x2026",
        L"Iniciar sess\x00e3o", L"Criar conta", L"A iniciar sess\x00e3o\x2026", L"A abrir\x2026", L"Terminar sess\x00e3o",
        L"Tentar novamente", L"Cancelar", L"Fechar", L"Voltar", L"nixlabs.tech",
        L"Conta", L"Miss\x00f5es", L"Snowflakes",
        L"Descodifique a data de cria\x00e7\x00e3o e os campos brutos de qualquer ID do Discord",
        L"ID Snowflake", L"Cole um snowflake", L"Cole um ID Snowflake do Discord",
        L"Apenas d\x00edgitos, at\x00e9 64 bits. Descodificado localmente.",
        L"Introduza um snowflake v\x00e1lido do Discord do passado.",
        L"Criado em", L"Detalhes brutos do ID", L"ID bruto", L"Marca temporal Unix (ms)",
        L"Bits da marca temporal (ms desde a \x00e9poca)", L"ID do servidor", L"ID do processo", L"Incremento",
        L"Copiar", L"Copiado", L"Identidade Discord",
        L"A sua conta", L"Insira as suas credenciais nixlabs para continuar",
        L"Nome de utilizador, handle ou e-mail", L"Palavra-passe ou PIN",
        L"N\x00e3o existe nenhum token Discord guardado. Adicione um em A Minha Conta.",
        L"O seu token Discord \x00e9 inv\x00e1lido. Substitua-o em A Minha Conta.",
        L"Conta do Discord", L"ID do utilizador", L"Conta criada em", L"Perfil Nixlabs",
        L"Endere\x00e7o de e-mail", L"Bom estatuto", L"Estatuto limitado", L"Suspenso / Banido",
        L"Idioma", L"N\x00edvel",
        L"Miss\x00f5es do Discord", L"Descubra e conclua miss\x00f5es promocionais ativas do Discord",
        L"Sem miss\x00f5es", L"De momento n\x00e3o existem miss\x00f5es ativas para esta conta.",
        L"Concluir todas as miss\x00f5es dispon\x00edveis", L"Concluir miss\x00e3o", L"Atualizar lista",
        L"Conclu\x00edda", L"Em curso\x2026", L"Tarefa atual", L"Parar", L"Carregar mais",
        L"Ligar Discord", L"Nenhuma conta do Discord associada",
        L"Associe a sua conta do Discord para concluir e acompanhar miss\x00f5es.",
        L"Sess\x00e3o Direta", L"Token", L"E-mail ou Telem\x00f3vel", L"Palavra-passe",
        L"Token de Utilizador do Discord", L"Cole o seu token do Discord",
        L"O seu token \x00e9 encriptado com AES-256-GCM no Nixlabs Accounts.",
        L"Guardar e Associar", L"Desassociar", L"Gerir token na Minha Conta"
    }
};

inline LocaleId ResolveLocaleFromCode(const std::wstring& lang) {
    if (lang.empty()) return LocaleId::EN_UK;
    std::wstring clean;
    clean.reserve(lang.size());
    for (wchar_t c : lang) {
        if (c == L'_') clean.push_back(L'-');
        else if (c >= L'A' && c <= L'Z') clean.push_back(c - L'A' + L'a');
        else clean.push_back(c);
    }
    if (clean.find(L"fr") == 0) return LocaleId::FR_BE;
    if (clean == L"en-uk" || clean == L"en-gb") return LocaleId::EN_UK;
    if (clean.find(L"en") == 0) return LocaleId::EN_US;
    if (clean == L"pt-pt") return LocaleId::PT_PT;
    if (clean.find(L"pt") == 0) return LocaleId::PT_BR;
    if (clean.find(L"ja") == 0) return LocaleId::JA;
    return LocaleId::EN_UK;
}

inline const LocaleStrings& GetLocale(LocaleId id) {
    int idx = static_cast<int>(id);
    if (idx < 0 || idx > 5) idx = 0;
    return kLocales[idx];
}
