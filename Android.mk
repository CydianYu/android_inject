# һ��Android.mk file���ȱ��붨���LOCAL_PATH������
# �������ڿ������в���Դ�ļ�������������У��꺯����my-dir��, 
# �ɱ���ϵͳ�ṩ�����ڷ��ص�ǰ·����������Android.mk file�ļ���Ŀ¼����
LOCAL_PATH := $(call my-dir)
# CLEAR_VARS�ɱ���ϵͳ�ṩ��
# ָ����GNU MAKEFILEΪ��������LOCAL_XXX���������� LOCAL_MODULE, LOCAL_SRC_FILES, LOCAL_STATIC_LIBRARIES, �ȵ�...),��LOCAL_PATH �����Ǳ�Ҫ�ģ�
# ��Ϊ���еı�������ļ�����ͬһ��GNU MAKEִ�л����У����еı�������ȫ�ֵġ�
include $(CLEAR_VARS)
# LOCAL_MODULE�������붨�壬�Ա�ʶ����Android.mk�ļ���������ÿ��ģ�顣���Ʊ�����Ψһ�ģ����Ҳ������κοո�
# ע�����ϵͳ���Զ��������ʵ�ǰ׺�ͺ�׺�����仰˵��һ��������Ϊ'foo'�Ĺ����ģ�飬��������'libfoo.so'�ļ���
LOCAL_MODULE    := shark_inject

LOCAL_LDLIBS    :=  -llog
# LOCAL_SRC_FILES�������������Ҫ��������ģ���е�C��C++Դ�����ļ���ע�⣬�㲻���������г�ͷ�ļ��Ͱ����ļ���
# ��Ϊ����ϵͳ�����Զ�Ϊ���ҳ������͵��ļ��������г�ֱ�Ӵ��ݸ���������Դ�����ļ��ͺá�
LOCAL_SRC_FILES := shark_inject.c
# BUILD_EXECUTABLE ��ʾ��һ����ִ�г���ķ�ʽ���б���
# BUILD_SHARED_LIBRARY ��ʾ��̬���ӿ�ķ�ʽ���б���
include $(BUILD_EXECUTABLE)


