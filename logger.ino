#include <LiquidCrystal.h>
#include <Wire.h>

LiquidCrystal lcd(12, 5, 8, 9, 10, 6);

// PARAMETROS DE PINOUT
#define C1 2
#define C2 3
#define C3 4
#define L1 0x0D
#define L2 0x0E
#define L3 0x0F
#define L4 0x10

// PARÂMETROS DIVERSOS
#define VREF 1.1
#define FREQ 16000000
#define PRESCALER 256.0
#define OCRA 159
#define TEMPO_COLETA 9

// constante de uma contagem do timer
const float ciclo_temporizador = OCRA * PRESCALER / FREQ;

// variáveis para debouncing
uint16_t estado = 0xFFFE;
uint8_t coordenada[] = {0, 0};
char teclado[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '*', '0', '#'};
char mensagem_atual[10];

bool imprime_tecla = false;

// variáveis de memória
uint16_t ponteiro_memoria = 0x00;
uint16_t quantidade_armazenada = 0x00;

// coleta
uint16_t counter_coleta = 0;
float media_parcial = 0;
uint16_t quantidade_coletada = 0;
bool coleta_iniciada = false;
bool guardar_coleta = false;
byte coleta_array[2];

// display
char display_ativado = 0;
char mostra_display = false;
unsigned int counter_display = 0;
bool le_ultima_temperatura = false;
uint16_t ultima_temperatura = 0;

enum Teclado
{
  NENHUMA_PRESSIONADA,
  VERIFICA_PRESSIONADA,
  PRESSIONADA,
  VERIFICA_SOLTA
};

enum Mensagem
{
  RECEBE_INSTRUCAO,
  RECEBE_PARAMETRO,
};

Mensagem estado_mensagem = RECEBE_INSTRUCAO;
Teclado estado_teclado = NENHUMA_PRESSIONADA;

void setup()
{
  // CONFIGURAÇÃO DE PINOS
  cli();
  pinMode(C1, OUTPUT);       // entrada 01
  pinMode(C2, OUTPUT);       // entrada 02
  pinMode(C3, OUTPUT);       // enable ponte H
  pinMode(L1, INPUT_PULLUP); // entrada 01
  pinMode(L2, INPUT_PULLUP); // entrada 02
  pinMode(L3, INPUT_PULLUP); // enable ponte H
  pinMode(L4, INPUT_PULLUP); // enable ponte H
  pinMode(A3, INPUT);

  digitalWrite(C1, LOW);
  digitalWrite(C2, LOW);
  digitalWrite(C3, LOW);
  analogReference(INTERNAL);

  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print("Envie um Comando");
  lcd.setCursor(0, 1);
  lcd.print("Mensagem:");

  Serial.begin(9600);
  Wire.begin();

  configuracao_Timer0();
  mensagem_atual[0] = '\0';
  sei();

  // seta parâmetros iniciais
  quantidade_armazenada = le_memoria(0);
  ponteiro_memoria = (quantidade_armazenada % 1023 + 1) * 2;
  ultima_temperatura = le_memoria(ponteiro_memoria - 2);
}

void loop()
{

  if (mostra_display)
  {
    envia_temperatura();
    mostra_display = false;
  }

  if (guardar_coleta)
  {
    if (quantidade_armazenada == 65535)
    {
      lcd.setCursor(0, 1);
      lcd.print("Limite Excedido ");
      guardar_coleta = false;
      coleta_iniciada = false;
      return;
    }
    coleta_array[0] = (uint16_t)(media_parcial)&0x00FF;
    coleta_array[1] = ((uint16_t)(media_parcial) >> 8) & 0x00FF;
    escreve_bytes(ponteiro_memoria, coleta_array, 2);
    atualiza_ponteiro();

    coleta_array[0] = quantidade_armazenada & 0x00FF;
    coleta_array[1] = (quantidade_armazenada >> 8) & 0x00FF;

    media_parcial = round(media_parcial);
    // Serial.print("Guarda: ");

    // Serial.println(media_parcial/10.0);

    media_parcial = 0;
    quantidade_coletada = 0;
    counter_coleta = 0;
    guardar_coleta = false;
  }

  if (le_ultima_temperatura && le_memoria(ponteiro_memoria - 2))
  {
    ultima_temperatura = le_memoria(ponteiro_memoria - 2);
    escreve_bytes(0x00, coleta_array, 2);
    le_ultima_temperatura = false;
  }

  if (imprime_tecla)
  {
    ajusta_mensagem();
    imprime_tecla = false;
  }
  if (estado_teclado == PRESSIONADA)
  {
    verifica_pressionada();
  }
  if (estado_teclado == NENHUMA_PRESSIONADA)
  {
    verifica_coordenada();
  }
}

ISR(TIMER0_COMPA_vect)
{
  // verificacao se estado se manteve
  if (estado_teclado == VERIFICA_PRESSIONADA || estado_teclado == VERIFICA_SOLTA)
  {
    bool LINHA_PRESSIONADA = digitalRead(coordenada[0]);
    estado = estado << 1 | LINHA_PRESSIONADA;
    if (estado == 0xFFFF)
    {
      estado_teclado = NENHUMA_PRESSIONADA;
      coordenada[0] = 0;
      coordenada[1] = 0;
      estado = 0xFFFE;
    }
    if (estado == 0x0000)
    {
      if (estado_teclado == VERIFICA_PRESSIONADA)
      {
        imprime_tecla = true;
      }
      estado_teclado = PRESSIONADA;
      estado = 0xFFFE;
    }
  }

  // comando 3
  if (coleta_iniciada)
  {
    if (counter_coleta * ciclo_temporizador > TEMPO_COLETA)
    {
      guardar_coleta = true;
    }

    if (quantidade_coletada + 1 != 0)
    {
      uint16_t tmp = get_valor_adc();
      media_parcial = (media_parcial * quantidade_coletada + tmp) / (quantidade_coletada + 1);

      quantidade_coletada++;
    }

    counter_coleta++;
  }

  mostra_display = true;
  counter_display = 0;

  counter_display++;
}

// verifica se alguma tecla foi pressionada
void verifica_coordenada()
{
  for (uint8_t j = C1; j <= C3; j++)
  {
    digitalWrite(C1, j == C1 ? LOW : HIGH);
    digitalWrite(C2, j == C2 ? LOW : HIGH);
    digitalWrite(C3, j == C3 ? LOW : HIGH);

    for (uint8_t i = L1; i <= L4; i++)
    {
      if (!digitalRead(i))
      {
        coordenada[0] = i;
        coordenada[1] = j;
        estado_teclado = VERIFICA_PRESSIONADA;
        return;
      }
    }
  }
}

void atualiza_ponteiro()
{
  quantidade_armazenada++;
  ponteiro_memoria = ponteiro_memoria + 2;
  le_ultima_temperatura = true;

  return;
}

// verifica se a tecla pressionada foi solta
void verifica_pressionada()
{
  if (digitalRead(coordenada[0]))
  {
    estado_teclado = VERIFICA_SOLTA;
  }
}

// returna indice do array que representa a tecla
uint8_t get_tecla()
{
  uint8_t i = (coordenada[0] - L1) % 4;
  uint8_t j = (coordenada[1] - C1) % 3;

  return 3 * i + j;
}

void ajusta_mensagem()
{
  char tecla = teclado[get_tecla()];

  if (tecla == '*')
  {
    mensagem_atual[0] = '\0';
    if (estado_mensagem == RECEBE_INSTRUCAO)
    {
      lcd.setCursor(0, 0);
      lcd.print("Envie um Comando");
      lcd.setCursor(0, 1);
      lcd.print("Mensagem:       ");
    }
    lcd.setCursor(9, 1);
    lcd.print("       ");

    return;
  }

  if (tecla == '#')
  {
    verifica_mensagem();

    return;
  }

  byte length = strlen(mensagem_atual);
  mensagem_atual[length] = tecla;
  mensagem_atual[length + 1] = '\0';

  if (estado_mensagem == RECEBE_INSTRUCAO)
  {
    if (strcmp(mensagem_atual, "1") == 0)
    {
      lcd.setCursor(0, 0);
      lcd.print("      Reset     ");
    }
    else if (strcmp(mensagem_atual, "2") == 0)
    {
      lcd.setCursor(0, 0);
      lcd.print("    Ocupacao    ");
    }
    else if (strcmp(mensagem_atual, "3") == 0)
    {
      lcd.setCursor(0, 0);
      lcd.print(" Iniciar coleta ");
    }
    else if (strcmp(mensagem_atual, "4") == 0)
    {
      lcd.setCursor(0, 0);
      lcd.print("Finalizar coleta");
    }
    else if (strcmp(mensagem_atual, "5") == 0)
    {
      lcd.setCursor(0, 0);
      lcd.print("Transferir dados");
    }
    else
    {
      lcd.setCursor(0, 0);
      lcd.print("Envie um Comando");
    }

    lcd.setCursor(0, 1);
    lcd.print("Mensagem:       ");
  }

  if (estado_mensagem == RECEBE_PARAMETRO)
  {
    lcd.setCursor(0, 1);
    lcd.print("Digite N:       ");
  }
  lcd.setCursor(9, 1);
  lcd.print(mensagem_atual);

  return;
}

void verifica_mensagem()
{
  if (estado_mensagem == RECEBE_INSTRUCAO)
  {
    if (strcmp(mensagem_atual, "1") == 0)
    {
      byte tmp_array[] = {0x00, 0x00};
      escreve_bytes(0x00, tmp_array, 2);
      ponteiro_memoria = 0x02;
      quantidade_armazenada = 0;
      lcd.setCursor(0, 1);
      lcd.print("    Resetado    ");

      Serial.println("Resetado");
    }
    else if (strcmp(mensagem_atual, "2") == 0)
    {
      uint16_t disponiveis = quantidade_armazenada >= 1023 ? 0 : 1023 - quantidade_armazenada;

      lcd.setCursor(0, 1);
      lcd.print("  G:");
      lcd.print(le_memoria(0));
      lcd.print(" D:");
      lcd.print(disponiveis);
      lcd.print("      ");
    }
    else if (strcmp(mensagem_atual, "3") == 0)
    {
      coleta_iniciada = true;
      Serial.println("Coleta Iniciada");
      lcd.setCursor(0, 1);
      lcd.print(" Coleta Iniciada ");
    }
    else if (strcmp(mensagem_atual, "4") == 0)
    {
      coleta_iniciada = false;
      quantidade_coletada = 0;
      media_parcial = 0;
      Serial.println("Coleta Finalizada");
      lcd.setCursor(0, 1);
      lcd.print("Coleta Finalizada");
    }
    else if (strcmp(mensagem_atual, "5") == 0)
    {
      estado_mensagem = RECEBE_PARAMETRO;
      lcd.setCursor(0, 1);
      lcd.print("Digite N:       ");
    }
    else
    {
      lcd.setCursor(0, 1);
      lcd.print("Mensagem Invalida");
    }

    mensagem_atual[0] = '\0';
    return;
  }

  if (estado_mensagem == RECEBE_PARAMETRO)
  {
    int N = string_2_int(mensagem_atual);
    uint16_t quantidade_registrada = quantidade_armazenada >= 1023 ? 1023 : quantidade_armazenada;
    if (N == -1 || N > quantidade_registrada)
    {

      lcd.setCursor(0, 1);
      lcd.print("Mensagem Invalida");

      mensagem_atual[0] = '\0';
      return;
    }

    uint16_t endereco_inicial = get_endereco_inicial(N);
    uint16_t counter = 1;
    uint16_t N_unico = N;
    for (int i = 0; i < 2 * N; i = i + 32)
    {
      if (i + 32 > 2 * N)
      {
        le_bytes(endereco_inicial + i, 2 * N - i, &N, &counter, N_unico);
      }
      else
      {
        le_bytes(endereco_inicial + i, 32, &N, &counter, N_unico);
      }
    }
    lcd.setCursor(0, 1);
    lcd.print("Dado Transferido");
    estado_mensagem = RECEBE_INSTRUCAO;
  }

  mensagem_atual[0] = '\0';
  return;
}

void escreve_byte(uint16_t address, uint8_t data)
{
  // separam o primeiro byte do segundo a ser enviado
  byte devaddr = 0x50 | ((address >> 8) & 0x07);
  byte addr = address & 0xFF;

  // realiza protocolo de byte write
  Wire.beginTransmission(devaddr);
  Wire.write(int(addr));
  Wire.write(data);

  Wire.endTransmission();
}

void escreve_bytes(uint16_t address, byte *data, byte length)
{
  // separam o primeiro byte do segundo a ser enviado
  byte devaddr = 0x50 | ((address >> 8) & 0x07);
  byte addr = address & 0xFF;

  // realiza protocolo de page write
  Wire.beginTransmission(devaddr);
  Wire.write(int(addr));

  for (int i = 0; i < length; i++)
  {
    Wire.write(data[i]);
  }

  Wire.endTransmission();
}

void le_bytes(uint16_t address, uint8_t n, uint16_t *N, uint16_t *counter, uint16_t N_unico)
{
  // separam o primeiro byte do segundo a ser enviado
  uint16_t endereco_atual;
  byte devaddr = 0x50 | ((address >> 8) & 0x07);
  byte addr = address & 0xFF;
  uint16_t temperatura = 0;

  // realiza protocolo de random read
  Wire.beginTransmission(devaddr);
  Wire.write(int(addr));

  Wire.endTransmission();

  Wire.requestFrom(int(devaddr), int(n));

  for (int i = 0; i < n && Wire.available(); i++)
  {

    endereco_atual = address + i;
    if ((endereco_atual) % 2048 == 0)
    {
      i++;
      *N = *N + 1;
      Wire.read();
      continue;
    }
    bool impar = (endereco_atual) % 2;
    if (!impar)
    {
      temperatura = 0;
    }

    temperatura |= ((uint16_t)Wire.read()) << (8 * impar);

    if (impar)
    {
      lcd.setCursor(0, 1);
      lcd.print(*counter);
      lcd.print(" de ");
      lcd.print(N_unico);
      lcd.print("  ");
      Serial.println(temperatura / 10.0);
      *counter = *counter + 1;
    }
  }
}

uint16_t le_memoria(uint16_t address)
{
  // separam o primeiro byte do segundo a ser enviado
  byte devaddr = 0x50 | ((address >> 8) & 0x07);
  byte addr = address & 0xFF;

  // realiza protocolo de random read
  Wire.beginTransmission(devaddr);
  Wire.write(int(addr));

  Wire.endTransmission();

  Wire.requestFrom(int(devaddr), int(2));

  uint16_t temperatura = 0;

  for (int i = 0; i < 2 && Wire.available(); i++)
  {
    temperatura |= ((uint16_t)Wire.read()) << (8 * i);
  }

  return temperatura;
}

uint16_t get_valor_adc()
{
  int adc = analogRead(A3);
  // multiplico por 10 para retornar o valor decimal
  return VREF * adc * 100.0 * 10 / (1023);
}

uint16_t get_endereco_inicial(uint16_t N)
{
  if (N == 1023)
  {
    return ponteiro_memoria;
  }
  int dif = ponteiro_memoria - 2 * N;
  if (dif < 2)
  {
    return 2046 + dif;
  }
  return dif;
}

int string_2_int(char *string)
{
  // transformação de string para inteiro
  int soma = 0;

  int j = 0;
  for (int i = (strlen(string) - 1); i >= 0; i--)
  {

    if (string[i] > 57 || string[i] < 48)
    {
      // parâmetro inválido
      return -1;
    }
    // string para decimal
    soma = round(soma + (string[j] - 48) * pow(10, i));

    j++;
  }

  return soma;
}

void escreve_8574(unsigned char display, unsigned char value)
{
  TIMSK0 |= (0 << OCIE0A);
  char data = (~(1 << display) << 4) | value;
  // inicia transmissão
  Wire.beginTransmission(0x20);
  // realizar operação de escrita no chip 8574
  Wire.write(data);
  // termina operação de transmissão
  Wire.endTransmission();
  TIMSK0 |= (1 << OCIE0A);
}

void envia_temperatura()
{
  float dummy_tmp = ultima_temperatura;

  // encontra digito a ser enviado
  char i = 0;
  int digito_temp = (int)(dummy_tmp) % 10;
  while (i < display_ativado)
  {
    dummy_tmp = floor(dummy_tmp / 10);
    digito_temp = (int)(dummy_tmp) % 10;

    i++;
  }

  // verifica se deve ativar ou não o display
  // garantir que variável temperatura seja ultima temperatura medida
  if (digito_temp == 0 && ultima_temperatura < pow(10, display_ativado))
  {
    escreve_8574(5, digito_temp);
  }
  else
  {
    escreve_8574(3 - display_ativado, digito_temp);
  }

  if (display_ativado == 3)
  {
    display_ativado = 0;
  }
  else
  {
    display_ativado++;
  }

  return;
}

void configuracao_Timer0()
{
  /////////////////////////////////////////////////////////////////////////////////////////////////////////////
  // Configuracao Temporizador 0 (8 bits)
  // Relogio = 16e6 Hz
  // Prescaler = 64
  // Faixa = 255
  // Intervalo entre interrupcoes: (Prescaler/Relogio)*Faixa = (64/16e6)*(256) = 0.0512s

  // TCCR0A – Timer/Counter Control Register A
  // COM2A1 COM2A0 COM2B1 COM2B0 – – WGM21 WGM20
  // 1     0      0      0          1     0
  TCCR0A = 0x82;

  // OCR2A – Output Compare Register A

  // TIMSK0 – Timer/Counter Interrupt Mask Register
  // – – – – – OCIE2B OCIE2A TOIE2
  // – – – – – 0      1      0
  TIMSK0 |= (1 << OCIE0A);
  OCR0A = OCRA;
  // TCCR2B – Timer/Counter Control Register B
  // FOC2A FOC2B – – WGM22 CS22 CS21 CS2
  // 0     0         0     0    1   1
  TCCR0B = 0x04;
  /////////////////////////////////////////////////////////////////////////////////////////////////////////////
}
