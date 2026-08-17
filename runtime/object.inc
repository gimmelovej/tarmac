# ================================================================================================
# File: object.inc — Layout do header de objeto (ver object.s), compartilhado entre os arquivos da
# runtime. Como cada `.s` é montado isoladamente (as ... -o *.o), estes offsets vivem aqui e são
# trazidos por `.include "object.inc"` onde forem usados (io.s, string.s, object.s).
# ================================================================================================
.equ OBJ_DATA, 0    # ponteiro para o payload
.equ OBJ_LEN,  8    # bytes usados
.equ OBJ_CAP,  16   # capacidade em bytes
.equ OBJ_SIZE, 24   # tamanho do header / início do payload
