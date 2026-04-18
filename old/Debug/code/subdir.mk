################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
"../code/All_init.c" \
"../code/All_init_core1.c" \
"../code/FUZZY_PID_UCAS.c" \
"../code/Motor.c" \
"../code/PID.c" \
"../code/Servo.c" \
"../code/ZiTaiJieSuan.c" \
"../code/camera.c" \
"../code/key.c" 

COMPILED_SRCS += \
"code/All_init.src" \
"code/All_init_core1.src" \
"code/FUZZY_PID_UCAS.src" \
"code/Motor.src" \
"code/PID.src" \
"code/Servo.src" \
"code/ZiTaiJieSuan.src" \
"code/camera.src" \
"code/key.src" 

C_DEPS += \
"./code/All_init.d" \
"./code/All_init_core1.d" \
"./code/FUZZY_PID_UCAS.d" \
"./code/Motor.d" \
"./code/PID.d" \
"./code/Servo.d" \
"./code/ZiTaiJieSuan.d" \
"./code/camera.d" \
"./code/key.d" 

OBJS += \
"code/All_init.o" \
"code/All_init_core1.o" \
"code/FUZZY_PID_UCAS.o" \
"code/Motor.o" \
"code/PID.o" \
"code/Servo.o" \
"code/ZiTaiJieSuan.o" \
"code/camera.o" \
"code/key.o" 


# Each subdirectory must supply rules for building sources it contributes
"code/All_init.src":"../code/All_init.c" "code/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2004 -D__CPU__=tc26xb "-fC:/Users/tym18/Desktop/final test/Seekfree_TC264_Opensource_Library_QianChe/Debug/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc26xb -Y0 -N0 -Z0 -o "$@" "$<"
"code/All_init.o":"code/All_init.src" "code/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
"code/All_init_core1.src":"../code/All_init_core1.c" "code/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2004 -D__CPU__=tc26xb "-fC:/Users/tym18/Desktop/final test/Seekfree_TC264_Opensource_Library_QianChe/Debug/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc26xb -Y0 -N0 -Z0 -o "$@" "$<"
"code/All_init_core1.o":"code/All_init_core1.src" "code/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
"code/FUZZY_PID_UCAS.src":"../code/FUZZY_PID_UCAS.c" "code/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2004 -D__CPU__=tc26xb "-fC:/Users/tym18/Desktop/final test/Seekfree_TC264_Opensource_Library_QianChe/Debug/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc26xb -Y0 -N0 -Z0 -o "$@" "$<"
"code/FUZZY_PID_UCAS.o":"code/FUZZY_PID_UCAS.src" "code/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
"code/Motor.src":"../code/Motor.c" "code/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2004 -D__CPU__=tc26xb "-fC:/Users/tym18/Desktop/final test/Seekfree_TC264_Opensource_Library_QianChe/Debug/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc26xb -Y0 -N0 -Z0 -o "$@" "$<"
"code/Motor.o":"code/Motor.src" "code/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
"code/PID.src":"../code/PID.c" "code/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2004 -D__CPU__=tc26xb "-fC:/Users/tym18/Desktop/final test/Seekfree_TC264_Opensource_Library_QianChe/Debug/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc26xb -Y0 -N0 -Z0 -o "$@" "$<"
"code/PID.o":"code/PID.src" "code/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
"code/Servo.src":"../code/Servo.c" "code/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2004 -D__CPU__=tc26xb "-fC:/Users/tym18/Desktop/final test/Seekfree_TC264_Opensource_Library_QianChe/Debug/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc26xb -Y0 -N0 -Z0 -o "$@" "$<"
"code/Servo.o":"code/Servo.src" "code/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
"code/ZiTaiJieSuan.src":"../code/ZiTaiJieSuan.c" "code/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2004 -D__CPU__=tc26xb "-fC:/Users/tym18/Desktop/final test/Seekfree_TC264_Opensource_Library_QianChe/Debug/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc26xb -Y0 -N0 -Z0 -o "$@" "$<"
"code/ZiTaiJieSuan.o":"code/ZiTaiJieSuan.src" "code/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
"code/camera.src":"../code/camera.c" "code/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2004 -D__CPU__=tc26xb "-fC:/Users/tym18/Desktop/final test/Seekfree_TC264_Opensource_Library_QianChe/Debug/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc26xb -Y0 -N0 -Z0 -o "$@" "$<"
"code/camera.o":"code/camera.src" "code/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"
"code/key.src":"../code/key.c" "code/subdir.mk"
	cctc -cs --dep-file="$*.d" --misrac-version=2004 -D__CPU__=tc26xb "-fC:/Users/tym18/Desktop/final test/Seekfree_TC264_Opensource_Library_QianChe/Debug/TASKING_C_C___Compiler-Include_paths__-I_.opt" --iso=99 --c++14 --language=+volatile --exceptions --anachronisms --fp-model=3 -O0 --tradeoff=4 --compact-max-size=200 -g -Wc-w544 -Wc-w557 -Ctc26xb -Y0 -N0 -Z0 -o "$@" "$<"
"code/key.o":"code/key.src" "code/subdir.mk"
	astc -Og -Os --no-warnings= --error-limit=42 -o  "$@" "$<"

clean: clean-code

clean-code:
	-$(RM) ./code/All_init.d ./code/All_init.o ./code/All_init.src ./code/All_init_core1.d ./code/All_init_core1.o ./code/All_init_core1.src ./code/FUZZY_PID_UCAS.d ./code/FUZZY_PID_UCAS.o ./code/FUZZY_PID_UCAS.src ./code/Motor.d ./code/Motor.o ./code/Motor.src ./code/PID.d ./code/PID.o ./code/PID.src ./code/Servo.d ./code/Servo.o ./code/Servo.src ./code/ZiTaiJieSuan.d ./code/ZiTaiJieSuan.o ./code/ZiTaiJieSuan.src ./code/camera.d ./code/camera.o ./code/camera.src ./code/key.d ./code/key.o ./code/key.src

.PHONY: clean-code

