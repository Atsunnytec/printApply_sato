/* Projeto Print Apply - Software desenvolvido para a
placa industrial V2.0 comunicando com a IHM - v1.0 */

// Funções Print & Apply - v1.0

#include <printApplyFunctions.h>

void setup()
{
  Serial.begin(115200);
  Serial.println("Print & Apply setup");

  mtx_ios = xSemaphoreCreateMutex();

  EEPROM.begin(EEPROM_SIZE);
  // restoreBackupParameters();
  // presetEEPROM();

  desligaTodosOutput();
  sensorDeProdutoOuStart.setup();

  createTasks();

  pinInitialization();
  ventiladorConfig();
  motorSetup();

  Serial.println("End Setup. Print & Apply Linear.");
}

void loop()
{
  motor.run();
  motor_espatula.run();

  // Ciclo:

  switch (fsm.estado)
  {
  case PARADA_EMERGENCIA_OLD:
  {
    static uint32_t timer_requestStatusImpressora = 0;

    if (millis() - timer_requestStatusImpressora >= 10000)
    {
      xTaskCreatePinnedToCore(t_requestStatusImpressoraZebra, "request status task", 4096, NULL, PRIORITY_1, NULL, CORE_0);
      timer_requestStatusImpressora = millis();
    }

    if (fsm.sub_estado == EMERGENCIA_TOP_OLD)
    {
      if (fsm_emergencia == fase1)
      {
        motor.stop();
        motor_espatula.stop();
        motorSetup();
        motorEnable();
        ventiladorWrite(VENTILADOR_CANAL, 0);
        fsm_emergencia = fase2;
      }
      else if (fsm_emergencia == fase2)
      {
        setBits(PIN_INTERTRAVAMENTO_OUT);
        setBits(LED_STATUS);
        ihm.focus(&menu_produto);
        ihm.showStatus2msg(F("-----EMERGENCIA-----"));
        vTaskResume(h_eeprom);
        fsm_emergencia = fase3;
      }
      else if (fsm_emergencia == fase3)
      {
        if (checkSensorEspatulaInit())
        {
          motor_espatula.move(-pulsosEspatulaRecuoInit);
          fsm_emergencia = fase4;
        }
        else
        {
          fsm_emergencia = fase5;
        }
      }
      else if (fsm_emergencia == fase4)
      {
        if (checkSensorEspatula())
        {
          motor_espatula.stop();
          fsm_emergencia = fase5;
        }
      }
      else if (fsm_emergencia == fase5)
      {
        if (checkSensorHomeInit())
        {
          pulsosBracoEmergencia = posicaoBracoEmergencia * resolucao;
          motor.moveTo(-pulsosBracoEmergencia);
          fsm_emergencia = fase8;
        }
        else
        {
          motor.move(pulsosBracoMaximo);
          fsm_emergencia = fase6;
        }
      }
      else if (fsm_emergencia == fase6)
      {
        if (checkSensorHome())
        {
          posicaoBracoSensor = motor.currentPosition();
          motor.stop();
          fsm_emergencia = fase7;
        }
      }
      else if (fsm_emergencia == fase7)
      {
        if (motor.distanceToGo() == 0)
        {
          posicaoBracoReferencia = motor.currentPosition();
          posicaoBracoCorrecao = posicaoBracoReferencia - posicaoBracoSensor;
          motor.setCurrentPosition(posicaoBracoCorrecao);
          fsm_emergencia = fase8;
        }
      }
      else if (fsm_emergencia == fase8)
      {
        pulsosBracoEmergencia = posicaoBracoEmergencia * resolucao;
        motor.moveTo(-pulsosBracoEmergencia);
        fsm_emergencia = fase9;
      }
      else if (fsm_emergencia == fase9)
      {
        if (motor.distanceToGo() == 0)
        {
          motorDisable();
          fsm_emergencia = fase10;
          flag_manutencao = true;
          fsm_manutencao = fase1;
        }
      }
    }

    if (fsm.sub_estado == MANUTENCAO_OLD)
    {
      if (fsm_manutencao == fase1)
      {
        ihm.showStatus2msg(F("-----MANUTENCAO-----"));
        liberaMenusDeManutencao();

        fsm_emergencia = fase1;
        fsm_manutencao = fase2;
      }
    }

    if (flag_emergencia == false) // Condição para sair de Parada de Emergência
    {
      if (flag_intertravamentoIn)
      {
        fsm.estado = ERRO_OLD;
        fsm_erro_intertravamento = fase1;
        fsm_erro_aplicacao = fase10;
        fsm_erro_impressora = fase10;
      }

      if (flag_statusImpressora)
      {
        fsm.estado = ERRO_OLD;
        fsm_erro_impressora = fase1;
        fsm_erro_aplicacao = fase10;
        fsm_erro_intertravamento = fase10;
      }

      if (fsm.sub_estado == MANUTENCAO_OLD)
      {
        bloqueiaMenusDeManutencao();
        fsm.sub_estado = EMERGENCIA_TOP_OLD;
        flag_manutencao = false;
      }
      else
      {
        flag_continuo = false;
        flag_manutencao = false;

        fsm.estado = ATIVO_OLD;
        fsm.sub_estado = REFERENCIANDO_INIT_OLD;

        fsm_emergencia = fase1;
        fsm_manutencao = fase1;

        fsm_referenciando_init = fase1;
      }
    }

    break;
  }
  case ATIVO_OLD:
  {
    static uint32_t timer_atrasoSensorProduto = 0;
    static uint32_t timer_etiqueta = 0;
    static uint32_t timer_ciclo = 0;
    static uint32_t timer_enableMotor = 0;
    static uint32_t timer_finalizarAplicacao = 0;
    static uint32_t timer_reinicio_espatula = 0;

    if (flag_intertravamentoIn)
    {
      fsm.estado = ERRO_OLD;
      fsm_erro_intertravamento = fase1;
      fsm_erro_aplicacao = fase10;
      fsm_erro_impressora = fase10;
    }

    if (flag_statusImpressora)
    {
      fsm.estado = ERRO_OLD;
      fsm_erro_impressora = fase1;
      fsm_erro_intertravamento = fase10;
      fsm_erro_aplicacao = fase10;
    }

    if (fsm.sub_estado == REFERENCIANDO_INIT_OLD)
    {
      if (fsm_referenciando_init == fase1)
      {
        motorSetup();
        motorEnable();

        ihm.focus(&menu_contador);
        ihm.showStatus2msg(F("-REFERENCIANDO INIT-"));

        fsm_referenciando_init = fase2;
        fsm_referenciando_init_espatula = fase2;
        fsm_referenciando_init_motor = fase2;

        timer_enableMotor = millis();
      }

      if (fsm_referenciando_init_motor == fase2)
      {
        if (millis() - timer_enableMotor >= tempoParaEstabilizarMotorBraco)
        {
          fsm_referenciando_init_motor = fase3;
        }
      }
      else if (fsm_referenciando_init_motor == fase3)
      {
        if (checkSensorHomeInit())
        {
          motor.move(-pulsosBracoForaSensor);
          fsm_referenciando_init_motor = fase4;
        }
        else
        {
          motor.move(pulsosBracoMaximo);
          fsm_referenciando_init_motor = fase5;
        }
      }
      else if (fsm_referenciando_init_motor == fase4)
      {
        if (motor.distanceToGo() == 0)
        {
          motor.move(pulsosBracoMaximo);
          fsm_referenciando_init_motor = fase5;
        }
      }
      else if (fsm_referenciando_init_motor == fase5)
      {
        if (checkSensorHome())
        {
          posicaoBracoSensor = motor.currentPosition();
          motor.stop();
          fsm_referenciando_init_motor = fase6;
        }
      }
      else if (fsm_referenciando_init_motor == fase6)
      {
        if (motor.distanceToGo() == 0)
        {
          posicaoBracoReferencia = motor.currentPosition();
          posicaoBracoCorrecao = posicaoBracoReferencia - posicaoBracoSensor;
          motor.setCurrentPosition(posicaoBracoCorrecao);
          fsm_referenciando_init_motor = fase7;
        }
      }

      if (fsm_referenciando_init_espatula == fase2)
      {
        if (checkSensorEspatulaInit())
        {
          motor_espatula.move(-pulsosEspatulaRecuoInit);
          fsm_referenciando_init_espatula = fase4;
        }
        else
        {
          motor_espatula.move(pulsosEspatulaAvancoInit);
          fsm_referenciando_init_espatula = fase3;
        }
      }
      else if (fsm_referenciando_init_espatula == fase3)
      {
        if (motor_espatula.distanceToGo() == 0)
        {
          motor_espatula.move(-pulsosEspatulaRecuoInit);
          fsm_referenciando_init_espatula = fase4;
        }
      }
      else if (fsm_referenciando_init_espatula == fase4)
      {
        if (checkSensorEspatula())
        {
          posicaoEspatulaSensor = motor_espatula.currentPosition();
          motor_espatula.stop();
          fsm_referenciando_init_espatula = fase5;
        }
      }
      else if (fsm_referenciando_init_espatula == fase5)
      {
        if (motor_espatula.distanceToGo() == 0)
        {
          posicaoEspatulaReferencia = motor_espatula.currentPosition();
          posicaoEspatulaCorrecao = posicaoEspatulaReferencia - posicaoEspatulaSensor;
          motor_espatula.setCurrentPosition(posicaoEspatulaCorrecao);
          fsm_referenciando_init_espatula = fase6;
        }
      }

      else if (fsm_referenciando_init_espatula == fase6 && fsm_referenciando_init_motor == fase7)
      {
        pulsosBracoInicial = posicaoBracoInicial * resolucao;
        motor.moveTo(-pulsosBracoInicial);
        motor_espatula.moveTo(pulsosEspatulaAvanco);

        ventiladorWrite(VENTILADOR_CANAL, 100);

        fsm_referenciando_init = fase3;
        fsm_referenciando_init_espatula = fase7;
        fsm_referenciando_init_motor = fase8;

        Serial.println("REFERENCIANDO INIT -- Fase Mista...");
      }

      else if (fsm_referenciando_init == fase3)
      {
        fsm.sub_estado = PRONTO_OLD;
        fsm_pronto_init = fase1;
        fsm_pronto_ciclo = fase4;
        fsm_referenciando_init = fase4;
      }
    }

    if (fsm.sub_estado == REFERENCIANDO_CICLO_OLD)
    {
      if (fsm_referenciando_ciclo == fase1)
      {
        ihm.showStatus2msg(F("REFERENCIANDO CICLO"));

        fsm_referenciando_ciclo = fase2;
        fsm_referenciando_ciclo_espatula = fase2;
        fsm_referenciando_ciclo_motor = fase2;
        timer_reinicio_espatula = millis();
      }

      if (fsm_referenciando_ciclo_motor == fase2)
      {
        pulsosBracoInicial = posicaoBracoInicial * resolucao;
        motor.moveTo(-pulsosBracoInicial);
        fsm_referenciando_ciclo_motor = fase3;
      }
      else if (fsm_referenciando_ciclo_motor == fase3)
      {
        if (motor.distanceToGo() == 0)
        {
          fsm_referenciando_ciclo_motor = fase4;
        }
      }

      if (fsm_referenciando_ciclo_espatula == fase2)
      {
        if (millis() - timer_reinicio_espatula >= tempoReinicioEspatula)
        {
          fsm_referenciando_ciclo_espatula = fase3;
        }
      }
      else if (fsm_referenciando_ciclo_espatula == fase3)
      {
        if (checkSensorEspatulaInit())
        {
          motor_espatula.move(-pulsosEspatulaRecuoInit);
          fsm_referenciando_ciclo_espatula = fase5;
        }
        else
        {
          motor_espatula.move(pulsosEspatulaAvancoInit);
          fsm_referenciando_ciclo_espatula = fase4;
        }
      }
      else if (fsm_referenciando_ciclo_espatula == fase4)
      {
        if (motor_espatula.distanceToGo() == 0)
        {
          motor_espatula.move(-pulsosEspatulaRecuoInit);
          fsm_referenciando_ciclo_espatula = fase5;
        }
      }
      else if (fsm_referenciando_ciclo_espatula == fase5)
      {
        if (checkSensorEspatula())
        {
          posicaoEspatulaSensor = motor_espatula.currentPosition();
          motor_espatula.stop();
          fsm_referenciando_ciclo_espatula = fase6;
        }
      }
      else if (fsm_referenciando_ciclo_espatula == fase6)
      {
        if (motor_espatula.distanceToGo() == 0)
        {
          posicaoEspatulaReferencia = motor_espatula.currentPosition();
          posicaoEspatulaCorrecao = posicaoEspatulaReferencia - posicaoEspatulaSensor;
          motor_espatula.setCurrentPosition(posicaoEspatulaCorrecao);
          fsm_referenciando_ciclo_espatula = fase7;
        }
      }

      else if (fsm_referenciando_ciclo_espatula == fase7 && fsm_referenciando_ciclo_motor == fase4)
      {
        motor_espatula.moveTo(pulsosEspatulaAvanco);
        ventiladorWrite(VENTILADOR_CANAL, 100);

        fsm_referenciando_ciclo_espatula = fase8;
        Serial.println("REFERENCIANDO CICLO_OLD -- Fase MISTA...");
      }

      else if (fsm_referenciando_ciclo_espatula == fase8)
      {
        if (motor_espatula.distanceToGo() == 0)
        {
          imprimirZebra();
          timer_etiqueta = millis();
          fsm_referenciando_ciclo_espatula = fase9;
        }
      }
      else if (fsm_referenciando_ciclo_espatula == fase9)
      {
        if (millis() - timer_etiqueta >= atrasoImpressaoEtiqueta)
        {
          motor_espatula.moveTo(-pulsosEspatulaRecuo);
          fsm_referenciando_ciclo_espatula = fase10;
        }
      }
      else if (fsm_referenciando_ciclo_espatula == fase10)
      {
        if (checkSensorEspatula())
        {
          posicaoEspatulaSensor = motor_espatula.currentPosition();
          motor_espatula.stop();
          fsm_referenciando_ciclo_espatula = fase11;
          fsm_referenciando_ciclo_motor = fase5;
        }
      }

      else if (fsm_referenciando_ciclo_motor == fase5)
      {
        pulsosBracoAplicacao = posicaoBracoAplicacao * resolucao;
        motor.moveTo(-pulsosBracoAplicacao);
        fsm_referenciando_ciclo_motor = fase6;
      }

      else if (fsm_referenciando_ciclo == fase2)
      {
        if (fsm_referenciando_ciclo_espatula == fase11 && fsm_referenciando_ciclo_motor == fase6)
        {
          if (motor.distanceToGo() == 0 && motor_espatula.distanceToGo() == 0)
          {
            fsm.sub_estado = PRONTO_OLD;
            fsm_pronto_ciclo = fase1;
            fsm_pronto_init = fase8;
            fsm_referenciando_ciclo = fase1;
            fsm_referenciando_ciclo_espatula = fase12;
            fsm_referenciando_ciclo_motor = fase7;
            Serial.println("REFERENCIANDO CICLO_OLD Motor -- Fim de referencia ciclo...");
          }
        }
      }
    }

    if (fsm.sub_estado == PRONTO_OLD)
    {
      if (fsm_pronto_init == fase1)
      {
        resetBits(LED_STATUS);
        vTaskResume(h_eeprom);
        ihm.focus(&menu_contador);
        ihm.showStatus2msg(F("APERTE O BOTAO START"));
        fsm_pronto_init = fase2;
      }
      else if (fsm_pronto_init == fase2)
      {
        if (checkBotaoStart()) // Inicia o ciclo com o botão START acionado e entra em modo contínuo
        {
          if (motor_espatula.distanceToGo() == 0)
          {
            ihm.showStatus2msg(F("APROXIMANDO BRACO..."));
            // imprimirZebra();
            timer_etiqueta = millis();
            fsm_pronto_init = fase3;
          }
        }
        else if (flag_continuo)
        {
          if (motor_espatula.distanceToGo() == 0)
          {
            ihm.showStatus2msg(F("APROXIMANDO BRACO..."));
            ventiladorWrite(VENTILADOR_CANAL, 100);
            imprimirZebra();
            timer_etiqueta = millis();
            fsm_pronto_init = fase3;
          }
        }
      }
      else if (fsm_pronto_init == fase3)
      {
        if (millis() - timer_etiqueta >= atrasoImpressaoEtiqueta)
        {
          motor_espatula.moveTo(-pulsosEspatulaRecuo);
          fsm_pronto_init = fase4;
        }
      }
      else if (fsm_pronto_init == fase4)
      {
        if (checkSensorEspatula())
        {
          posicaoEspatulaSensor = motor_espatula.currentPosition();
          motor_espatula.stop();
          fsm_pronto_init = fase5;
        }
      }
      else if (fsm_pronto_init == fase5)
      {
        pulsosBracoAplicacao = posicaoBracoAplicacao * resolucao;
        motor.moveTo(-pulsosBracoAplicacao);

        fsm_pronto_init = fase6;
      }
      else if (fsm_pronto_init == fase6)
      {
        if (motor.distanceToGo() == 0)
        {
          ihm.showStatus2msg(F("AGUARDANDO PRODUTO.."));
          fsm_pronto_init = fase7;
        }
      }
      else if (fsm_pronto_init == fase7)
      {
        motorRun();
        if (checkSensorProduto())
        {
          ihm.showStatus2msg(F("--------CICLO-------"));
          fsm.sub_estado = CICLO_OLD;
          fsm_ciclo = fase1;
          fsm_pronto_init = fase8;
        }
      }

      if (fsm_pronto_ciclo == fase1)
      {
        Serial.print("Tempo de Ciclo: ");
        Serial.println(millis() - timer_ciclo);
        fsm_pronto_ciclo = fase2;
      }
      else if (fsm_pronto_ciclo == fase2)
      {
        ihm.showStatus2msg(F("AGUARDANDO PRODUTO.."));
        ihm.focus(&menu_contador);
        fsm_pronto_ciclo = fase3;
      }
      else if (fsm_pronto_ciclo == fase3)
      {
        motorRun();
        if (checkSensorProduto())
        {
          ihm.showStatus2msg(F("--------CICLO-------"));
          fsm.sub_estado = CICLO_OLD;
          fsm_ciclo = fase1;
          fsm_pronto_ciclo = fase4;
        }
      }
    }

    if (fsm.sub_estado == CICLO_OLD)
    {
      if (fsm_ciclo == fase1)
      {
        vTaskSuspend(h_eeprom);
        timer_atrasoSensorProduto = millis();
        timer_ciclo = millis();
        fsm_ciclo = fase2;
      }
      else if (fsm_ciclo == fase2)
      {
        if (millis() - timer_atrasoSensorProduto >= atrasoSensorProduto)
        {
          pulsosBracoProduto = posicaoBracoProduto * resolucao;
          motor.moveTo(-pulsosBracoProduto);
          fsm_ciclo = fase3;
        }
      }
      else if (fsm_ciclo == fase3)
      {
        if (checkSensorAplicacao())
        {
          posicaoBracoDeteccaoProduto = motor.currentPosition();
          pulsosBracoEspacamento = espacamentoProdutomm * resolucao;
          fsm_ciclo = fase4;
        }
        else if (motor.distanceToGo() == 0)
        {
          fsm.estado = ERRO_OLD;
          fsm_erro_aplicacao = fase1;
          fsm_erro_intertravamento = fase10;
          fsm_erro_impressora = fase10;
        }
      }
      else if (fsm_ciclo == fase4)
      {
        if (pulsosBracoEspacamento <= pulsosRampa)
        {
          pulsosBracoEspacamento = pulsosRampa + 1000;
          fsm_ciclo = fase5;
        }
        else
        {
          fsm_ciclo = fase5;
        }
      }
      else if (fsm_ciclo == fase5)
      {
        if (motor.currentPosition() == (posicaoBracoDeteccaoProduto - (pulsosBracoEspacamento - pulsosRampa)))
        {
          motor.stop();
          fsm_ciclo = fase6;
        }
        else if (motor.distanceToGo() == 0)
        {
          fsm.estado = ERRO_OLD;
          fsm_erro_aplicacao = fase1;
          fsm_erro_intertravamento = fase10;
          fsm_erro_impressora = fase10;
        }
      }
      else if (fsm_ciclo == fase6)
      {
        if (motor.distanceToGo() == 0)
        {
          // ventiladorWrite(VENTILADOR_CANAL, 25);
          fsm_ciclo = fase7;
        }
      }
      else if (fsm_ciclo == fase7)
      {
        incrementaContador();
        fsm_ciclo = fase8;
        timer_finalizarAplicacao = millis();
      }
      else if (fsm_ciclo == fase8)
      {
        if (millis() - timer_finalizarAplicacao >= tempoFinalizarAplicacao)
        {
          fsm.sub_estado = REFERENCIANDO_CICLO_OLD;
          fsm_referenciando_ciclo = fase1;
          fsm_ciclo = fase9;
        }
      }
    }

    // Condição para sair de ATIVO_OLD:
    if (flag_emergencia == true)
    {
      fsm.estado = PARADA_EMERGENCIA_OLD;
      fsm.sub_estado = EMERGENCIA_TOP_OLD;
    }
    // Condição para sair de ATIVO_OLD:
    break;
  }
  case ERRO_OLD:
  {
    piscaLedStatus();

    if (fsm_erro_aplicacao == fase1)
    {
      ihm.showStatus2msg(F("PRODUTO N ENCONTRADO"));
      ventiladorWrite(VENTILADOR_CANAL, 0);
      fsm_erro_aplicacao = fase2;
      Serial.println("Erro de Aplicacao...");
    }
    else if (fsm_erro_aplicacao == fase2)
    {
      fsm.estado = ATIVO_OLD;
      fsm.sub_estado = REFERENCIANDO_INIT_OLD;
      fsm_referenciando_init = fase1;
      fsm_erro_aplicacao = fase4;
      flag_continuo = true;
    }

    if (fsm_erro_impressora == fase1)
    {
      ihm.showStatus2msg(F("IMPRESSORA COM ERRO"));
      ventiladorWrite(VENTILADOR_CANAL, 0);
      fsm_erro_impressora = fase2;
      Serial.println("IMPRESSORA DESABILITADA OU COM ERRO...");
    }
    else if (fsm_erro_impressora == fase2)
    {
      if (flag_statusImpressora == false)
      {
        fsm.estado = ATIVO_OLD;
        fsm.sub_estado = REFERENCIANDO_INIT_OLD;
        fsm_referenciando_init = fase1;
        fsm_erro_impressora = fase4;
      }
    }

    if (fsm_erro_intertravamento == fase1)
    {
      motor.stop();
      motor_espatula.stop();
      motorDisable();
      ventiladorWrite(VENTILADOR_CANAL, 0);
      fsm_erro_intertravamento = fase2;
    }
    else if (fsm_erro_intertravamento == fase2)
    {
      if (flag_intertravamentoIn)
      {
        ihm.showStatus2msg(F("INTERTRAV. DESCONECT"));
        fsm_erro_intertravamento = fase3;
      }
    }
    else if (fsm_erro_intertravamento == fase3)
    {
      if (flag_intertravamentoIn == false)
      {
        fsm.estado = ATIVO_OLD;
        fsm.sub_estado = REFERENCIANDO_INIT_OLD;
        fsm_referenciando_init = fase1;
        fsm_erro_intertravamento = fase4;
      }
    }

    // Condição para sair do ERRO_OLD:
    if (flag_emergencia == true)
    {
      fsm.estado = PARADA_EMERGENCIA_OLD;
      fsm.sub_estado = EMERGENCIA_TOP_OLD;
    }
    // Condição para sair do ERRO_OLD:
    break;
  }
  case ESTADO_TESTE:
  {
    static uint32_t fsm_teste = fase1;
    static uint32_t timer_duracaoPrint = 0;
    static uint32_t timer_delayPosPrint = 0;
    const uint16_t duracaoPrint = 1500;  // ms
    const uint16_t delayPosPrint = 5000; // ms

    if (fsm_teste == fase1)
    {
      if(sensorDeProdutoOuStart.checkPulse())
      {
        Serial.println("pulse");
      }
    }
    else if (fsm_teste == fase2)
    {
      
    }
    else if(fsm_teste == fase3)
    {
      
      
    }
    break;
  }
  }
}
