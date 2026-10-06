#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <mysql/mysql.h>
#include <fcntl.h>

#define BUF_SIZE 1024

// ==================================================
// 수거 판정
// 기준값보다 이만큼(%p) 이상 낮은 값이 2회 연속 오면 수거한 것으로 본다.
// 한 번만 낮게 튀었다가 돌아오면(80 → 50 → 80) 수거로 치지 않는다.
// 예: 85 → 60 (1회째, 대기) → 58 (2회째) = 수거
// ==================================================
#define COLLECT_DROP_PERCENT 20
#define COLLECT_CONFIRM_COUNT 2
#define BIN_COUNT 3

static const char *BIN_NAMES[BIN_COUNT] = { "A", "B", "C" };

// 통별 기준값 (수거 전 포화도). 서버 시작 시 food_bin 마지막 행에서 불러온다
static int base_percent[BIN_COUNT];
static int has_base = 0;

// 통별 "기준값보다 20 이상 낮은 값"이 연속으로 온 횟수
static int low_count[BIN_COUNT];

// ==================================================
// Bluetooth -> STM32 전송 함수
//
// 예:
// STATUS@89@55@20\n
// ==================================================
void send_bluetooth_status(int a, int b, int c)
{
    int bt_fd;
    char bt_msg[64];

    // STM32가 받을 문자열 생성
    snprintf(
        bt_msg,
        sizeof(bt_msg),
        "STATUS@%d@%d@%d\n",
        a,
        b,
        c
    );

    // HC-06 RFCOMM 장치 열기
    bt_fd = open("/dev/rfcomm0", O_WRONLY | O_NOCTTY);

    if (bt_fd == -1)
    {
        perror("Bluetooth open");
        return;
    }

    // STM32로 전송
    if (write(bt_fd, bt_msg, strlen(bt_msg)) == -1)
    {
        perror("Bluetooth write");
    }
    else
    {
        printf("BT SEND : %s", bt_msg);
    }

    close(bt_fd);
}


// ==================================================
// 서버 시작 시 food_bin 마지막 행을 기준값으로 불러오기
//
// 서버가 꺼져 있는 동안 수거한 것도 다음 수신 때 판정할 수 있다.
// 테이블이 비어 있으면 첫 수신 값을 기준값으로 쓴다.
// ==================================================
void load_base_from_db(MYSQL *conn)
{
    MYSQL_RES *result;
    MYSQL_ROW row;

    if (mysql_query(conn,
            "SELECT bin_a, bin_b, bin_c FROM food_bin "
            "ORDER BY id DESC LIMIT 1"))
    {
        fprintf(stderr, "BASE LOAD ERROR : %s\n", mysql_error(conn));
        return;
    }

    result = mysql_store_result(conn);

    if (result == NULL)
    {
        return;
    }

    row = mysql_fetch_row(result);

    if (row != NULL &&
        row[0] != NULL &&
        row[1] != NULL &&
        row[2] != NULL)
    {
        for (int i = 0; i < BIN_COUNT; i++)
        {
            base_percent[i] = atoi(row[i]);
        }

        has_base = 1;

        printf(
            "BASE LOADED : A=%d%% B=%d%% C=%d%%\n",
            base_percent[0],
            base_percent[1],
            base_percent[2]
        );
    }
    else
    {
        printf("BASE : food_bin is empty, use first data\n");
    }

    mysql_free_result(result);
}


// ==================================================
// collection_history에 한 줄 저장
// collected_at은 테이블 기본값(CURRENT_TIMESTAMP)으로 자동 저장.
// ==================================================
void save_collection(MYSQL *conn, const char *bin_name)
{
    char query[128];

    snprintf(
        query,
        sizeof(query),
        "INSERT INTO collection_history (bin_name) VALUES ('%s')",
        bin_name
    );

    if (mysql_query(conn, query))
    {
        fprintf(
            stderr,
            "HISTORY INSERT ERROR : %s\n",
            mysql_error(conn)
        );
    }
    else
    {
        printf("HISTORY INSERT SUCCESS\n");
    }
}


// ==================================================
// 수거 판정
//
// 기준값보다 COLLECT_DROP_PERCENT 이상 낮은 값이
// COLLECT_CONFIRM_COUNT번 연속으로 오면 수거로 기록한다.
//   - 낮은 값이 오는 동안에는 기준값을 바꾸지 않는다 (수거 전 값과 비교)
//   - 중간에 원래대로 돌아오면 횟수를 0으로 되돌린다 (센서 튐 무시)
//   - 수거로 기록하면 지금 값이 새 기준값이 된다
// ==================================================
void check_collection(MYSQL *conn, const int cur[BIN_COUNT])
{
    if (!has_base)
    {
        // 기준값이 없으면 (DB가 비어 있던 경우) 첫 데이터를 기준값으로
        memcpy(base_percent, cur, sizeof(base_percent));
        has_base = 1;
        return;
    }

    for (int i = 0; i < BIN_COUNT; i++)
    {
        int drop = base_percent[i] - cur[i];

        if (drop < COLLECT_DROP_PERCENT)
        {
            // 평소 상태: 횟수 초기화, 기준값 갱신
            low_count[i] = 0;
            base_percent[i] = cur[i];
            continue;
        }

        low_count[i]++;

        if (low_count[i] < COLLECT_CONFIRM_COUNT)
        {
            printf(
                "COLLECT PENDING : bin %s (%d%% -> %d%%, %d/%d)\n",
                BIN_NAMES[i],
                base_percent[i],
                cur[i],
                low_count[i],
                COLLECT_CONFIRM_COUNT
            );
            continue;
        }

        printf(
            "COLLECTED : bin %s (%d%% -> %d%%)\n",
            BIN_NAMES[i],
            base_percent[i],
            cur[i]
        );

        save_collection(conn, BIN_NAMES[i]);

        low_count[i] = 0;
        base_percent[i] = cur[i];
    }
}


// ==================================================
// MAIN
// ==================================================
int main(int argc, char *argv[])
{
    int serv_sock;
    int clnt_sock;
    int len;
    int option = 1;

    struct sockaddr_in serv_addr;
    struct sockaddr_in clnt_addr;
    socklen_t clnt_addr_size;

    char buf[BUF_SIZE];
    char query[256];

    MYSQL *conn;


    // ==================================================
    // 실행 인자 확인
    // ==================================================
    if (argc != 2)
    {
        printf("Usage : %s <port>\n", argv[0]);
        exit(1);
    }


    // ==================================================
    // 1. MariaDB 연결
    // ==================================================
    conn = mysql_init(NULL);

    if (conn == NULL)
    {
        fprintf(stderr, "mysql_init() failed\n");
        exit(1);
    }

    if (mysql_real_connect(
            conn,
            "127.0.0.1",   // DB 서버
            "iot",         // DB 계정
            "pwiot",       // DB 비밀번호
            "iotdb",       // DB 이름
            0,
            NULL,
            0) == NULL)
    {
        fprintf(
            stderr,
            "DB CONNECT ERROR : %s\n",
            mysql_error(conn)
        );

        mysql_close(conn);
        exit(1);
    }

    printf("MariaDB Connected\n");


    // 수거 판정 기준값: food_bin 마지막 행
    load_base_from_db(conn);


    // ==================================================
    // 2. TCP 서버 소켓 생성
    // ==================================================
    serv_sock = socket(PF_INET, SOCK_STREAM, 0);

    if (serv_sock == -1)
    {
        perror("socket");

        mysql_close(conn);
        exit(1);
    }


    // 서버 재실행 시
    // Address already in use 방지
    setsockopt(
        serv_sock,
        SOL_SOCKET,
        SO_REUSEADDR,
        &option,
        sizeof(option)
    );


    memset(&serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;

    // Raspberry Pi의 모든 네트워크 인터페이스에서 접속 허용
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // 실행할 때 입력한 PORT
    serv_addr.sin_port = htons(atoi(argv[1]));


    // ==================================================
    // 3. IP + PORT 등록
    // ==================================================
    if (bind(
            serv_sock,
            (struct sockaddr *)&serv_addr,
            sizeof(serv_addr)) == -1)
    {
        perror("bind");

        close(serv_sock);
        mysql_close(conn);

        exit(1);
    }


    // ==================================================
    // 4. 클라이언트 연결 대기
    // ==================================================
    if (listen(serv_sock, 5) == -1)
    {
        perror("listen");

        close(serv_sock);
        mysql_close(conn);

        exit(1);
    }


    printf("================================\n");
    printf(" Food Waste TCP Server Start\n");
    printf(" Raspberry Pi : 10.10.16.70\n");
    printf(" Port         : %s\n", argv[1]);
    printf("================================\n");


    // ==================================================
    // 서버 반복 실행
    // ==================================================
    while (1)
    {
        clnt_addr_size = sizeof(clnt_addr);


        // ==================================================
        // 5. Arduino 접속 대기
        // ==================================================
        clnt_sock = accept(
            serv_sock,
            (struct sockaddr *)&clnt_addr,
            &clnt_addr_size
        );

        if (clnt_sock == -1)
        {
            perror("accept");
            continue;
        }


        printf(
            "\nClient Connected : %s\n",
            inet_ntoa(clnt_addr.sin_addr)
        );


        // ==================================================
        // 6. Arduino 데이터 수신
        //
        // TCP는 read() 한 번에 전체 문자열이 들어온다는
        // 보장이 없기 때문에 '\n'까지 계속 받음.
        // ==================================================
        memset(buf, 0, BUF_SIZE);

        int total = 0;


        while (total < BUF_SIZE - 1)
        {
            len = read(
                clnt_sock,
                &buf[total],
                BUF_SIZE - 1 - total
            );

            if (len <= 0)
            {
                break;
            }


            total += len;

            buf[total] = '\0';


            // Arduino가 마지막에 '\n'을 전송
            if (strchr(buf, '\n') != NULL)
            {
                break;
            }
        }


        // ==================================================
        // 데이터가 정상적으로 들어온 경우
        // ==================================================
        if (total > 0)
        {
            printf("RECV : %s", buf);


            // ==================================================
            // 7. SENSOR@82@37@91 파싱
            // ==================================================
            char *ptr;

            ptr = strtok(buf, "@\r\n");


            if (ptr != NULL &&
                strcmp(ptr, "SENSOR") == 0)
            {
                char *a_str;
                char *b_str;
                char *c_str;


                a_str = strtok(NULL, "@\r\n");
                b_str = strtok(NULL, "@\r\n");
                c_str = strtok(NULL, "@\r\n");


                // A/B/C 데이터가 모두 존재하는지 확인
                if (a_str != NULL &&
                    b_str != NULL &&
                    c_str != NULL)
                {
                    int bin_a = atoi(a_str);
                    int bin_b = atoi(b_str);
                    int bin_c = atoi(c_str);


                    printf("A = %d%%\n", bin_a);
                    printf("B = %d%%\n", bin_b);
                    printf("C = %d%%\n", bin_c);


                    // ==================================================
                    // 7-1. 수거 판정 (기준값보다 20%p 이상 낮은 값이 2회 연속)
                    // ==================================================
                    int cur[BIN_COUNT] = { bin_a, bin_b, bin_c };

                    check_collection(conn, cur);


                    // ==================================================
                    // 8. SQL INSERT 생성
                    // ==================================================
                    snprintf(
                        query,
                        sizeof(query),
                        "INSERT INTO food_bin "
                        "(bin_a, bin_b, bin_c) "
                        "VALUES (%d, %d, %d)",
                        bin_a,
                        bin_b,
                        bin_c
                    );


                    // ==================================================
                    // 9. MariaDB 저장
                    // ==================================================
                    if (mysql_query(conn, query))
                    {
                        fprintf(
                            stderr,
                            "DB INSERT ERROR : %s\n",
                            mysql_error(conn)
                        );
                    }
                    else
                    {
                        printf("DB INSERT SUCCESS\n");


                        // ==============================================
                        // 10. Bluetooth -> STM32 전송
                        //
                        // SENSOR@89@55@20
                        //       ↓
                        // STATUS@89@55@20
                        // ==============================================
                        send_bluetooth_status(
                            bin_a,
                            bin_b,
                            bin_c
                        );
                    }
                }
                else
                {
                    printf("ERROR : Invalid SENSOR data\n");
                }
            }
            else
            {
                printf("ERROR : Unknown command\n");
            }
        }
        else
        {
            printf("ERROR : No data received\n");
        }


        // ==================================================
        // 11. Arduino TCP 연결 종료
        // ==================================================
        close(clnt_sock);
    }


    // while(1)이므로 일반적으로 여기까지 오지 않음
    close(serv_sock);
    mysql_close(conn);

    return 0;
}
